/* Production pause/command transport with real threads; the bounded engine
 * callbacks below observe thread identity and explicitly controlled races. */
#include <android/native_window.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "ctr_host.h"
#include "ctr_mystery.h"

void ANativeWindow_acquire(ANativeWindow *w) { (void)w; }
void ANativeWindow_release(ANativeWindow *w) { (void)w; }
int __android_log_print(int p, const char *t, const char *f, ...)
{ (void)p; (void)t; (void)f; return 0; }
bool CtrHost_ShowShinyFleePrompt(uint32_t r) { (void)r; return false; }

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_t gameThread;
static bool entered, gate, exited, holdAction, actionEntered, releaseAction, requestDone;
static bool claimed;
static unsigned frames, queryCalls, actionCalls, mutations, queryCount = 7;
static int asyncResult;

static struct timespec Deadline(int ms)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_REALTIME, &t) == 0);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { ++t.tv_sec; t.tv_nsec -= 1000000000L; }
    return t;
}

static void WaitFor(bool *value)
{
    struct timespec end = Deadline(2000);
    pthread_mutex_lock(&lock);
    while (!*value) assert(pthread_cond_timedwait(&cond, &lock, &end) == 0);
    pthread_mutex_unlock(&lock);
}

void CtrHost_NotifyGameExit(int status)
{
    assert(status == 0);
    pthread_mutex_lock(&lock);
    exited = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

int TestGameMain(void)
{
    pthread_mutex_lock(&lock);
    gameThread = pthread_self();
    entered = true;
    pthread_cond_broadcast(&cond);
    while (!gate) pthread_cond_wait(&cond, &lock);
    pthread_mutex_unlock(&lock);
    while (CtrHost_GetState() != CTR_HOST_EXITING)
    {
        if (CtrHost_WaitWhilePaused() == CTR_HOST_EXITING) break;
        pthread_mutex_lock(&lock);
        ++frames;
        pthread_mutex_unlock(&lock);
        struct timespec tick = {0, 1000000};
        nanosleep(&tick, NULL);
    }
    return 0;
}

unsigned CtrMystery_Query(int *states, unsigned capacity)
{
    assert(pthread_equal(pthread_self(), gameThread));
    assert(CtrHost_GetState() == CTR_HOST_PAUSED);
    assert(CtrHost_GameSpeed() == 1); /* Also proves host lock is not held. */
    pthread_mutex_lock(&lock);
    ++queryCalls;
    unsigned count = queryCount;
    for (unsigned i = 0; i < capacity && i < count; ++i)
        states[i] = i == 2 && claimed ? CTR_MYSTERY_UNLOCKED : CTR_MYSTERY_AVAILABLE;
    pthread_mutex_unlock(&lock);
    return count;
}

int CtrMystery_Activate(unsigned eventId)
{
    assert(pthread_equal(pthread_self(), gameThread));
    assert(CtrHost_GetState() == CTR_HOST_PAUSED);
    pthread_mutex_lock(&lock);
    ++actionCalls;
    if (eventId != 2 || claimed)
    {
        int result = eventId != 2 ? CTR_MYSTERY_RESULT_INVALID : CTR_MYSTERY_RESULT_ALREADY;
        pthread_mutex_unlock(&lock);
        return result;
    }
    actionEntered = true;
    pthread_cond_broadcast(&cond);
    /* A slow callback is injected only to make execution races observable.
     * The production adapters are finite in-memory operations, never waiters. */
    while (holdAction && !releaseAction) pthread_cond_wait(&cond, &lock);
    claimed = true;
    ++mutations;
    pthread_mutex_unlock(&lock);
    return CTR_MYSTERY_RESULT_ACTIVATED;
}

static void *Activate(void *unused)
{
    (void)unused;
    int result = CtrHost_ActivateMysteryEvent(2, 10);
    pthread_mutex_lock(&lock);
    asyncResult = result;
    requestDone = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    int states[CTR_MYSTERY_MAX_EVENTS] = {0};
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 10) == 0);
    assert(CtrHost_ActivateMysteryEvent(2, 10) == CTR_MYSTERY_RESULT_NO_GAME);
    assert(CtrHost_ActivateMysteryEvent(32, 10) == CTR_MYSTERY_RESULT_INVALID);
    CtrHost_SetState(CTR_HOST_PAUSED);
    assert(CtrHost_StartGame());
    WaitFor(&entered);

    /* The game has not reached its pause point. Expired commands must never
     * run later when it finally arrives. */
    assert(CtrHost_ActivateMysteryEvent(2, 15) == CTR_MYSTERY_RESULT_TIMEOUT);
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 15) == 0);
    pthread_mutex_lock(&lock);
    gate = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    assert(CtrHost_WaitUntilPaused(2000));
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 1000) == 7);
    pthread_mutex_lock(&lock);
    assert(actionCalls == 0 && queryCalls == 1 && mutations == 0 && frames == 0);
    pthread_mutex_unlock(&lock);
    for (unsigned i = 0; i < 7; ++i) assert(states[i] == CTR_MYSTERY_AVAILABLE);

    /* Even if the game thread wins the wake-up race, a zero-deadline command
     * must be rejected by the consumer before entering the adapter. */
    for (unsigned i = 0; i < 32; ++i)
        assert(CtrHost_ActivateMysteryEvent(2, 0) == CTR_MYSTERY_RESULT_TIMEOUT);
    int small[] = {-1, -1, 0x1234};
    assert(CtrHost_QueryMysteryEvents(small, 2, 1000) == 7);
    assert(small[0] == 0 && small[1] == 0 && small[2] == 0x1234);
    pthread_mutex_lock(&lock);
    assert(actionCalls == 0 && mutations == 0 && frames == 0);
    queryCount = CTR_MYSTERY_MAX_EVENTS + 1;
    pthread_mutex_unlock(&lock);
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 1000) == 0);
    pthread_mutex_lock(&lock);
    queryCount = 7;
    holdAction = true;
    pthread_mutex_unlock(&lock);

    pthread_t caller;
    assert(pthread_create(&caller, NULL, Activate, NULL) == 0);
    WaitFor(&actionEntered);
    assert(CtrHost_ActivateMysteryEvent(2, 10) == CTR_MYSTERY_RESULT_BUSY);
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 10) == 0);
    assert(!CtrHost_WaitUntilPaused(0));
    pthread_mutex_lock(&lock);
    struct timespec end = Deadline(40);
    while (!requestDone)
        if (pthread_cond_timedwait(&cond, &lock, &end) == ETIMEDOUT) break;
    assert(!requestDone); /* No ambiguous timeout after execution began. */
    pthread_mutex_unlock(&lock);
    CtrHost_SetState(CTR_HOST_RUNNING);
    pthread_mutex_lock(&lock);
    releaseAction = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    WaitFor(&requestDone);
    assert(pthread_join(caller, NULL) == 0 && asyncResult == CTR_MYSTERY_RESULT_ACTIVATED);
    assert(CtrHost_ActivateMysteryEvent(2, 10) == CTR_MYSTERY_RESULT_BUSY);

    CtrHost_SetState(CTR_HOST_PAUSED);
    assert(CtrHost_WaitUntilPaused(2000));
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 1000) == 7);
    assert(states[2] == CTR_MYSTERY_UNLOCKED);
    assert(CtrHost_ActivateMysteryEvent(2, 1000) == CTR_MYSTERY_RESULT_ALREADY);
    assert(CtrHost_ActivateMysteryEvent(31, 1000) == CTR_MYSTERY_RESULT_INVALID);
    pthread_mutex_lock(&lock);
    assert(mutations == 1);
    pthread_mutex_unlock(&lock);
    CtrHost_SetState(CTR_HOST_EXITING);
    WaitFor(&exited);
    assert(CtrHost_QueryMysteryEvents(states, CTR_MYSTERY_MAX_EVENTS, 10) == 0);
    assert(CtrHost_ActivateMysteryEvent(2, 10) == CTR_MYSTERY_RESULT_NO_GAME);
    puts("mystery host: game-thread ownership, paused service without frames, bounded snapshot, queued expiry/no late activation, concurrent callers, exact started result, resume and exit passed");
    return 0;
}
