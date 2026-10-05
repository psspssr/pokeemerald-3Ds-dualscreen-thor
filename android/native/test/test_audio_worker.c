/* Real upstream worker + real pthread/futex shim. The mix and unused device
 * output are controlled adapters; this does not establish audible quality. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <3ds/thread.h>
#include <3ds/synchronization.h>
#include <3ds/services/apt.h>
#include <3ds/os.h>
#include <3ds/result.h>
#include "3ds_audio.h"
#include "3ds_platform.h"

static bool rejectCore2, syntheticWait;
static u64 syntheticTicks;
static unsigned core2Attempts, core1Attempts;
static atomic_int mixes, mixing, engineHeld, lockStarted, lockDone, kickStarted, kickDone;
static atomic_int shutdownStarted, shutdownDone;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gateChanged = PTHREAD_COND_INITIALIZER;
static bool released;

Thread __real_threadCreate(ThreadFunc entry, void *arg, size_t stack, int priority, int core, bool detached);
Thread __wrap_threadCreate(ThreadFunc entry, void *arg, size_t stack, int priority, int core, bool detached)
{
    if (core == 2) {
        ++core2Attempts;
        if (rejectCore2) return NULL;
    }
    if (core == 1) ++core1Attempts;
    return __real_threadCreate(entry, arg, stack, priority, core, detached);
}

u64 __real_svcGetSystemTick(void);
u64 __wrap_svcGetSystemTick(void)
{
    if (!syntheticWait) return __real_svcGetSystemTick();
    /* A deterministic 2ms wait observation per Kick; not a speed benchmark. */
    syntheticTicks += SYSCLOCK_ARM11 / 500u;
    return syntheticTicks;
}

int __android_log_write(int priority, const char *tag, const char *text)
{ (void)priority; (void)tag; (void)text; return 0; }
void CtrLog_Write(CtrLogCategory category, const char *format, ...)
{ (void)category; (void)format; }
void ndspChnWaveBufClear(int channel) { (void)channel; assert(!"unexpected device-output path"); }
void ndspExit(void) { assert(!"unexpected device-output path"); }
void linearFree(void *data) { (void)data; assert(!"unexpected device-output allocation"); }

#include "audio_worker_actual.inc"

static void await(atomic_int *value, int wanted)
{
    for (unsigned i = 0; i < 3000 && atomic_load(value) < wanted; ++i) usleep(1000);
    assert(atomic_load(value) >= wanted);
}

static void releaseMix(void)
{
    pthread_mutex_lock(&gate);
    released = true;
    pthread_cond_broadcast(&gateChanged);
    pthread_mutex_unlock(&gate);
}

static void mix(void)
{
    assert(!atomic_load(&engineHeld));
    atomic_store(&mixing, 1);
    atomic_fetch_add(&mixes, 1);
    pthread_mutex_lock(&gate);
    while (!released) pthread_cond_wait(&gateChanged, &gate);
    pthread_mutex_unlock(&gate);
    assert(!atomic_load(&engineHeld));
    atomic_store(&mixing, 0);
}

static void *lockEngine(void *arg)
{
    (void)arg;
    atomic_store(&lockStarted, 1);
    CtrAudio_LockSound();
    CtrAudio_LockSound();
    assert(!atomic_load(&mixing));
    atomic_store(&engineHeld, 1);
    usleep(1000);
    atomic_store(&engineHeld, 0);
    CtrAudio_UnlockSound();
    CtrAudio_UnlockSound();
    atomic_store(&lockDone, 1);
    return NULL;
}

static void *kickAgain(void *arg)
{
    (void)arg;
    atomic_store(&kickStarted, 1);
    assert(CtrAudio_Kick());
    atomic_store(&kickDone, 1);
    return NULL;
}

static void *shutdownWorker(void *arg)
{
    (void)arg;
    atomic_store(&shutdownStarted, 1);
    CtrAudio_Shutdown();
    atomic_store(&shutdownDone, 1);
    return NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "fallback")) {
        rejectCore2 = syntheticWait = true;
        released = true;
        assert(CtrAudio_StartWorker(mix));
        assert(core2Attempts == 1 && core1Attempts == 1);
        u32 percent = 0;
        assert(APT_GetAppCpuTimeLimit(&percent) == 0 && percent == CTR_AUDIO_SYSCORE_PERCENT);
        for (unsigned i = 1; i < CTR_AUDIO_WAIT_WINDOW; ++i) assert(CtrAudio_Kick());
        assert(!CtrAudio_Kick());
        assert(atomic_load(&mixes) == CTR_AUDIO_WAIT_WINDOW - 1);
        CtrAudio_Shutdown();
        assert(sWorker == NULL);
        puts("PASS audio worker: unavailable core fallback, retained CPU hint and bounded inline-fallback handshake");
        return 0;
    }
    assert(CtrAudio_StartWorker(mix));
    assert(CtrAudio_Kick()); /* Returns while the first mix is still blocked. */
    await(&mixes, 1);
    assert(atomic_load(&mixing) && !released);
    if (!strcmp(argv[1], "shutdown")) {
        pthread_t closing;
        assert(!pthread_create(&closing, NULL, shutdownWorker, NULL));
        await(&shutdownStarted, 1);
        usleep(10000);
        assert(!atomic_load(&shutdownDone));
        releaseMix();
        assert(!pthread_join(closing, NULL));
        assert(atomic_load(&shutdownDone) && sWorker == NULL && !atomic_load(&mixing));
        LightEvent_Signal(&sKick);
        usleep(10000);
        assert(atomic_load(&mixes) == 1);
        puts("PASS audio worker: shutdown waits for the outstanding mix, joins it and rejects late wakeups");
        return 0;
    }
    assert(!strcmp(argv[1], "handoff"));
    pthread_t locker, kicker;
    assert(!pthread_create(&locker, NULL, lockEngine, NULL));
    assert(!pthread_create(&kicker, NULL, kickAgain, NULL));
    await(&lockStarted, 1);
    await(&kickStarted, 1);
    usleep(10000);
    assert(!atomic_load(&lockDone) && !atomic_load(&kickDone) && atomic_load(&mixes) == 1);
    releaseMix();
    assert(!pthread_join(locker, NULL) && !pthread_join(kicker, NULL));
    await(&mixes, 2);
    CtrAudio_Shutdown();
    assert(atomic_load(&lockDone) && atomic_load(&kickDone) && atomic_load(&mixes) == 2);
    puts("PASS audio worker: asynchronous handoff, previous-frame wait and recursive engine lock serialize mutations");
    return 0;
}
