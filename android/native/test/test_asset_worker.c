#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <3ds/thread.h>
#include <3ds/synchronization.h>
#include <3ds/services/apt.h>
#include <3ds/svc.h>
#include "3ds_platform.h"
#include "3ds_assets.h"

static atomic_int reads, reading, warmed, stopping, stopped, filesClosed, logClosed, lateAccess;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool released, packMode, failCreate;
static unsigned created;
static Thread workerHandle;

Thread __real_threadCreate(ThreadFunc entry, void *arg, size_t stack, int priority, int core, bool detached);
Thread __wrap_threadCreate(ThreadFunc entry, void *arg, size_t stack, int priority, int core, bool detached)
{
    assert(core == -2 && stack == 16 * 1024);
    if (failCreate) return NULL;
    created++;
    assert(detached == !TEST_FIXED);
    workerHandle = __real_threadCreate(entry, arg, stack, priority, core, detached);
    return workerHandle;
}
int __android_log_write(int priority, const char *tag, const char *text)
{ (void)priority; (void)tag; (void)text; return 0; }
void CtrLog_Write(CtrLogCategory category, const char *format, ...)
{
    if (atomic_load(&logClosed)) atomic_fetch_add(&lateAccess, 1);
    if (category == CTR_LOG_FS && !strncmp(format, "assets warmed", 13)) atomic_store(&warmed, 1);
}
u64 CtrPlatform_Ticks(void) { return svcGetSystemTick(); }
float CtrPlatform_TickMs(u64 ticks) { (void)ticks; return 0; }
#include "asset_types.inc"
static struct AssetMapEntry entries[8];
static struct AssetPayload payloads[8];
static struct AssetMapEntry *sEntries = entries;
static struct AssetPayload *sPayloads = payloads;
static u32 sEntryCount = 8, sBudget = CTR_ASSET_CACHE_BUDGET;
static struct CtrAssetStats sStats;
static CtrLock sLock;
static volatile u8 *sWarming;
#if TEST_FIXED
static CtrThread sWarmThread;
static bool sWarmStop;
#endif
#include "asset_thread_helpers.inc"

static const char *GetAssetPathByIndex(u32 index)
{
    static const char *paths[] = {"graphics/0", "graphics/1", "graphics/2", "graphics/3",
                                 "graphics/4", "graphics/5", "graphics/6", "graphics/7"};
    assert(index < sEntryCount); return paths[index];
}
static bool LoadAssetMap(void) { return true; }
static void Publish(u32 index, u8 *data)
{ assert(index < sEntryCount && data && !sPayloads[index].data); sPayloads[index].data = data; sStats.bytes += 8; sStats.loads++; }
static void readGate(void)
{
    atomic_fetch_add(&reads, 1); atomic_store(&reading, 1);
    pthread_mutex_lock(&gate);
    while (!released) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
    if (atomic_load(&filesClosed)) atomic_fetch_add(&lateAccess, 1);
}
static u8 *ReadPayload(u32 index)
{
    readGate();
    u8 *data = malloc(8); assert(data); memset(data, (int)index + 1, 8); return data;
}
bool CtrData_Locate(const char *path, u64 *offset, u32 *size)
{
    if (!packMode) return false;
    *offset = (u64)(path[strlen(path) - 1] - '0') * (256u * 1024u + 8);
    *size = 8; return true;
}
bool CtrData_ReadRange(u64 offset, void *destination, u32 size)
{
    assert(size == 8); readGate();
    memset(destination, (int)(offset / (256u * 1024u + 8)) + 1, size); return true;
}
#include "asset_worker.inc"

static bool sInitialized = true;
static CtrPlatformHooks sHooks;
static u64 sFrames;
static aptHookCookie sAptHook;
void CtrSettings_Shutdown(void) { assert(!atomic_load(&filesClosed)); }
void CtrVideo_Shutdown(void) {}
void CtrAudio_Shutdown(void) {}
void aptUnhook(aptHookCookie *hook) { assert(hook == &sAptHook); }
void gfxExit(void) {}
void CtrFs_Shutdown(void) { atomic_store(&filesClosed, 1); }
void CtrLog_Close(void) { atomic_store(&logClosed, 1); }
static void closeData(void) { atomic_store(&filesClosed, 1); }
#include "asset_shutdown.inc"

static void await(atomic_int *value, int wanted)
{
    for (unsigned i = 0; i < 3000 && atomic_load(value) < wanted; ++i) usleep(1000);
    assert(atomic_load(value) >= wanted);
}
static void releaseRead(void)
{ pthread_mutex_lock(&gate); released = true; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate); }
static void *shutdownThread(void *arg)
{ (void)arg; atomic_store(&stopping, 1); CtrPlatform_Shutdown(); atomic_store(&stopped, 1); return NULL; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    CtrLock_Init(&sLock); sHooks.shutdown = closeData;
    for (unsigned i = 0; i < sEntryCount; i++) entries[i].size = 8;
    failCreate = !strcmp(argv[1], "failure");
    bool finished = !strcmp(argv[1], "finished");
    packMode = !strcmp(argv[1], "pack");
    if (finished) releaseRead();
    CtrAssets_StartWarmup();
    if (failCreate) {
        assert(!created && !sWarming && !workerHandle);
        CtrPlatform_Shutdown();
        assert(atomic_load(&filesClosed) && atomic_load(&logClosed));
        puts("PASS asset warmup: failed thread creation releases staging and shutdown remains safe");
        return 0;
    }
    assert(created == 1);
    if (finished) {
        await(&warmed, 1); CtrPlatform_Shutdown();
        assert(atomic_load(&reads) == 8);
    } else {
        await(&reading, 1);
        pthread_t stopper; assert(!pthread_create(&stopper, NULL, shutdownThread, NULL));
        await(&stopping, 1);
        usleep(30000);
        bool premature = atomic_load(&filesClosed) || atomic_load(&logClosed) || atomic_load(&stopped);
        releaseRead();
        assert(!pthread_join(stopper, NULL)); await(&warmed, 1);
        fprintf(stderr, "actual %s shutdown: premature=%d, reads=%d, lateAccess=%d\n",
                argv[1], premature, atomic_load(&reads), atomic_load(&lateAccess));
        assert(!premature && atomic_load(&reads) == 1);
    }
    assert(!atomic_load(&lateAccess));
    assert(atomic_load(&filesClosed) && atomic_load(&logClosed) && !sWarming);
#if TEST_FIXED
    assert(!sWarmThread);
    CtrAssets_StopWarmup(); // Repeated stop is harmless and never double joins.
#endif
    CtrPlatform_Shutdown();
    for (unsigned i = 0; i < sEntryCount; i++) free(payloads[i].data);
    puts("PASS actual asset worker: bounded cancellation, complete join before teardown, no later access, repeat-safe stop");
    return 0;
}
