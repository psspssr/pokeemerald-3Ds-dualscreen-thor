/* Force a resume->pause round trip before the sleeping game can observe
 * RUNNING. The wrapper controls scheduling, not production state or locks. */
#include <android/native_window.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "ctr_host.h"

void ANativeWindow_acquire(ANativeWindow *window) { (void)window; }
void ANativeWindow_release(ANativeWindow *window) { (void)window; }
int __android_log_print(int priority, const char *tag, const char *format, ...)
{ (void)priority; (void)tag; (void)format; return 0; }
bool CtrHost_ShowShinyFleePrompt(uint32_t request) { (void)request; return false; }

static pthread_mutex_t gateLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gateCond = PTHREAD_COND_INITIALIZER;
static bool woke, releaseWake, exited;
static _Thread_local bool isGameThread;
static atomic_bool gateNextWake = true;

static struct timespec deadline(void)
{
    struct timespec end;
    assert(clock_gettime(CLOCK_REALTIME, &end) == 0);
    end.tv_sec += 2;
    return end;
}

static void waitFlag(bool *flag)
{
    struct timespec end = deadline();
    pthread_mutex_lock(&gateLock);
    while (!*flag)
        assert(pthread_cond_timedwait(&gateCond, &gateLock, &end) == 0);
    pthread_mutex_unlock(&gateLock);
}

int __real_pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex);
int __wrap_pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
    int result = __real_pthread_cond_wait(condition, mutex);
    if (isGameThread && atomic_exchange(&gateNextWake, false))
    {
        /* Let the UI win the host mutex before the game inspects the new
         * state, as happens when both state changes precede a wakeup. */
        pthread_mutex_unlock(mutex);
        pthread_mutex_lock(&gateLock);
        woke = true;
        pthread_cond_broadcast(&gateCond);
        struct timespec end = deadline();
        while (!releaseWake)
            assert(pthread_cond_timedwait(&gateCond, &gateLock, &end) == 0);
        pthread_mutex_unlock(&gateLock);
        pthread_mutex_lock(mutex);
    }
    return result;
}

int TestGameMain(void)
{
    isGameThread = true;
    assert(CtrHost_WaitWhilePaused() == CTR_HOST_EXITING);
    return 0;
}

void CtrHost_NotifyGameExit(int status)
{
    assert(status == 0);
    pthread_mutex_lock(&gateLock);
    exited = true;
    pthread_cond_broadcast(&gateCond);
    pthread_mutex_unlock(&gateLock);
}

int main(void)
{
    CtrHost_SetState(CTR_HOST_PAUSED);
    assert(CtrHost_StartGame());
    assert(CtrHost_WaitUntilPaused(1000));
    CtrHost_SetState(CTR_HOST_RUNNING);
    waitFlag(&woke);
    CtrHost_SetState(CTR_HOST_PAUSED);
    pthread_mutex_lock(&gateLock);
    releaseWake = true;
    pthread_cond_broadcast(&gateCond);
    pthread_mutex_unlock(&gateLock);
    bool acknowledged = CtrHost_WaitUntilPaused(1000);
    printf("renewed pause acknowledged: %s\n", acknowledged ? "yes" : "no");
    fflush(stdout);
    assert(acknowledged && "a renewed pause must be acknowledged before waiting again");
    assert(CtrHost_GetState() == CTR_HOST_PAUSED);
    CtrHost_SetState(CTR_HOST_EXITING);
    waitFlag(&exited);
    puts("PASS real game-thread pause acknowledgement after a coalesced resume/pause");
    return 0;
}
