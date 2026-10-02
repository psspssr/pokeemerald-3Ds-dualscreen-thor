/*
 * Shared host state (ctr_host.h). The UI thread writes it through the JNI
 * bridge; the game thread reads it through the shim. One mutex guards
 * everything; the condition variable wakes the game thread on state changes
 * and the UI thread when the game thread has seen a window change.
 */
#include <android/native_window.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ctr_host.h"
#include "ctr_host_internal.h"
#include "ctr_mystery.h"

#define GAME_THREAD_STACK (8u * 1024u * 1024u)
/* How long surfaceDestroyed waits for the game thread to drop the window. */
#define WINDOW_RELEASE_TIMEOUT_MS 250

extern int main(void);
/* The production shim supplies cleanup. The standalone host harness has no
 * libctru layer and only needs the normal game-exit notification. */
extern void CtrShim_Exit(int status) __attribute__((weak, noreturn));
/* The display-only harness has no engine or save state. */
extern unsigned CtrMystery_Query(int *, unsigned) __attribute__((weak));
extern int CtrMystery_Activate(unsigned) __attribute__((weak));

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sCond = PTHREAD_COND_INITIALIZER;

static char sRomfsDir[PATH_MAX];
static char sSdmcDir[PATH_MAX];
static bool sStarted;
static pthread_t sGameThread;

typedef struct
{
    struct ANativeWindow *window;
    uint32_t generation;
    /* The last generation the game thread asked for. */
    uint32_t seen;
} HostWindow;

static HostWindow sWindows[CTR_HOST_MAX_WINDOWS];

static CtrHostLayout sLayout;
static CtrHostInput sInput;
static CtrHostState sState = CTR_HOST_RUNNING;
static bool sPauseAcknowledged;
static unsigned sGameSpeed = 1, sShinyMultiplier = 1;
static bool sSharedExperience, sSaveBackups, sProtectShinies;
static uint32_t sPromptSequence, sPromptPending;
static bool sPromptAllow;

enum MysteryPhase { MYSTERY_IDLE, MYSTERY_QUEUED, MYSTERY_EXECUTING, MYSTERY_DONE };
static enum MysteryPhase sMysteryPhase;
static bool sMysteryActivate;
static unsigned sMysteryEvent, sMysteryCount;
static int sMysteryResult, sMysteryStates[CTR_MYSTERY_MAX_EVENTS];
static struct timespec sMysteryDeadline;
static pthread_once_t sMysteryOnce = PTHREAD_ONCE_INIT;
static pthread_cond_t sMysteryCond;
static bool sMysteryCondReady;

static void InitMysteryCond(void)
{
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) return;
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0)
        sMysteryCondReady = pthread_cond_init(&sMysteryCond, &attr) == 0;
    pthread_condattr_destroy(&attr);
}

static void CopyPath(char *dst, const char *src)
{
    size_t len;

    snprintf(dst, PATH_MAX, "%s", src ? src : "");
    len = strlen(dst);
    while (len > 1 && dst[len - 1] == '/')
        dst[--len] = '\0';
}

void CtrHost_SetPaths(const char *romfsDir, const char *sdmcDir)
{
    pthread_mutex_lock(&sLock);
    /* The game thread reads the paths without locking once it runs. */
    if (!sStarted)
    {
        CopyPath(sRomfsDir, romfsDir);
        CopyPath(sSdmcDir, sdmcDir);
    }
    pthread_mutex_unlock(&sLock);
}

const char *CtrHost_RomfsDir(void)
{
    return sRomfsDir;
}

const char *CtrHost_SdmcDir(void)
{
    return sSdmcDir;
}

static void DeadlineAfterMs(struct timespec *ts, int ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static bool ValidIndex(int index)
{
    return index >= 0 && index < CTR_HOST_MAX_WINDOWS;
}

void CtrHost_SetWindowAt(int index, struct ANativeWindow *window)
{
    HostWindow *slot;
    struct ANativeWindow *old;

    if (!ValidIndex(index))
        return;
    pthread_mutex_lock(&sLock);
    slot = &sWindows[index];
    if (window == slot->window)
    {
        pthread_mutex_unlock(&sLock);
        return;
    }
    if (window)
        ANativeWindow_acquire(window);
    old = slot->window;
    slot->window = window;
    slot->generation++;
    pthread_cond_broadcast(&sCond);
    /*
     * After surfaceDestroyed returns the surface must not be drawn to. Give a
     * running game thread a frame to notice the new generation (it asks for
     * it at least once per frame) and drop its EGL surface.
     */
    if (!window && sStarted && sState == CTR_HOST_RUNNING)
    {
        struct timespec deadline;
        uint32_t wanted = slot->generation;

        DeadlineAfterMs(&deadline, WINDOW_RELEASE_TIMEOUT_MS);
        while (slot->seen != wanted && sState == CTR_HOST_RUNNING)
            if (pthread_cond_timedwait(&sCond, &sLock, &deadline) == ETIMEDOUT)
                break;
    }
    pthread_mutex_unlock(&sLock);
    if (old)
        ANativeWindow_release(old);
}

void CtrHost_SetWindow(struct ANativeWindow *window)
{
    CtrHost_SetWindowAt(0, window);
}

static void MarkSeen(HostWindow *slot)
{
    if (slot->seen != slot->generation)
    {
        slot->seen = slot->generation;
        pthread_cond_broadcast(&sCond);
    }
}

struct ANativeWindow *CtrHost_AcquireWindowAt(int index, uint32_t *generation)
{
    struct ANativeWindow *window = NULL;

    if (!ValidIndex(index))
    {
        if (generation)
            *generation = 0;
        return NULL;
    }
    pthread_mutex_lock(&sLock);
    window = sWindows[index].window;
    if (window)
        ANativeWindow_acquire(window);
    if (generation)
        *generation = sWindows[index].generation;
    MarkSeen(&sWindows[index]);
    pthread_mutex_unlock(&sLock);
    return window;
}

uint32_t CtrHost_WindowGenerationAt(int index)
{
    uint32_t generation;

    if (!ValidIndex(index))
        return 0;
    pthread_mutex_lock(&sLock);
    generation = sWindows[index].generation;
    pthread_mutex_unlock(&sLock);
    return generation;
}

struct ANativeWindow *CtrHost_AcquireWindow(uint32_t *generation)
{
    return CtrHost_AcquireWindowAt(0, generation);
}

uint32_t CtrHost_WindowGeneration(void)
{
    return CtrHost_WindowGenerationAt(0);
}

void CtrHost_SetLayout(const CtrHostLayout *layout)
{
    pthread_mutex_lock(&sLock);
    sLayout = *layout;
    if (!ValidIndex(sLayout.topWindow))
        sLayout.topWindow = 0;
    if (!ValidIndex(sLayout.bottomWindow))
        sLayout.bottomWindow = 0;
    pthread_mutex_unlock(&sLock);
}

void CtrHost_GetLayout(CtrHostLayout *out)
{
    pthread_mutex_lock(&sLock);
    *out = sLayout;
    pthread_mutex_unlock(&sLock);
}

void CtrHost_SetInput(const CtrHostInput *input)
{
    pthread_mutex_lock(&sLock);
    sInput = *input;
    pthread_mutex_unlock(&sLock);
}

void CtrHost_GetInput(CtrHostInput *out)
{
    pthread_mutex_lock(&sLock);
    *out = sInput;
    pthread_mutex_unlock(&sLock);
}

void CtrHost_SetGameplayOptions(unsigned speed, unsigned shinyMultiplier,
                               bool sharedExperience, bool saveBackups, bool protectShinies)
{
    if (speed < 1 || speed > 4) speed = 1;
    if (!shinyMultiplier || shinyMultiplier > 64 || (shinyMultiplier & (shinyMultiplier - 1)))
        shinyMultiplier = 1;
    pthread_mutex_lock(&sLock);
    sGameSpeed = speed;
    sShinyMultiplier = shinyMultiplier;
    sSharedExperience = sharedExperience;
    sSaveBackups = saveBackups;
    sProtectShinies = protectShinies;
    pthread_mutex_unlock(&sLock);
}

unsigned CtrHost_GameSpeed(void)
{
    pthread_mutex_lock(&sLock);
    unsigned value = sState == CTR_HOST_RUNNING ? sGameSpeed : 1;
    pthread_mutex_unlock(&sLock);
    return value;
}

unsigned CtrHost_ShinyMultiplier(void)
{
    pthread_mutex_lock(&sLock);
    unsigned value = sShinyMultiplier;
    pthread_mutex_unlock(&sLock);
    return value;
}

bool CtrHost_SharedExperience(void)
{
    pthread_mutex_lock(&sLock);
    bool value = sSharedExperience;
    pthread_mutex_unlock(&sLock);
    return value;
}

bool CtrHost_SaveBackups(void)
{
    pthread_mutex_lock(&sLock);
    bool value = sSaveBackups;
    pthread_mutex_unlock(&sLock);
    return value;
}

bool CtrHost_ProtectShinies(void)
{
    pthread_mutex_lock(&sLock);
    bool value = sProtectShinies;
    pthread_mutex_unlock(&sLock);
    return value;
}

void CtrHost_AnswerShinyFlee(uint32_t request, bool allow)
{
    pthread_mutex_lock(&sLock);
    if (request && sPromptPending == request)
    {
        sPromptAllow = allow && sState == CTR_HOST_RUNNING;
        sPromptPending = 0;
        pthread_cond_broadcast(&sCond);
    }
    pthread_mutex_unlock(&sLock);
}

bool CtrHost_IsShinyFleePending(uint32_t request)
{
    pthread_mutex_lock(&sLock);
    bool pending = request && sPromptPending == request && sState == CTR_HOST_RUNNING;
    pthread_mutex_unlock(&sLock);
    return pending;
}

bool CtrHost_ConfirmShinyFlee(void)
{
    pthread_mutex_lock(&sLock);
    if (sState != CTR_HOST_RUNNING || sPromptPending)
    {
        pthread_mutex_unlock(&sLock);
        return false;
    }
    if (!++sPromptSequence) ++sPromptSequence;
    uint32_t request = sPromptPending = sPromptSequence;
    sPromptAllow = false;
    pthread_mutex_unlock(&sLock);
    if (!CtrHost_ShowShinyFleePrompt(request))
        CtrHost_AnswerShinyFlee(request, false);
    pthread_mutex_lock(&sLock);
    while (sPromptPending == request && sState == CTR_HOST_RUNNING)
        pthread_cond_wait(&sCond, &sLock);
    bool allow = sPromptAllow && sState == CTR_HOST_RUNNING;
    pthread_mutex_unlock(&sLock);
    return allow;
}

void CtrHost_SetState(CtrHostState state)
{
    pthread_mutex_lock(&sLock);
    /* EXITING is final: the game cannot be resumed once told to quit. */
    if (sState != CTR_HOST_EXITING && sState != state)
    {
        sState = state;
        sPauseAcknowledged = false;
        if (state != CTR_HOST_PAUSED && sMysteryPhase == MYSTERY_QUEUED)
        {
            sMysteryResult = state == CTR_HOST_EXITING ? CTR_MYSTERY_RESULT_NO_GAME
                                                      : CTR_MYSTERY_RESULT_BUSY;
            sMysteryCount = 0;
            sMysteryPhase = MYSTERY_DONE;
            pthread_cond_broadcast(&sMysteryCond);
        }
        /* Keys held when the activity went away must not stay held. */
        if (state != CTR_HOST_RUNNING)
        {
            memset(&sInput, 0, sizeof(sInput));
            sPromptPending = 0;
            sPromptAllow = false;
        }
        pthread_cond_broadcast(&sCond);
    }
    pthread_mutex_unlock(&sLock);
}

CtrHostState CtrHost_GetState(void)
{
    CtrHostState state;

    pthread_mutex_lock(&sLock);
    state = sState;
    pthread_mutex_unlock(&sLock);
    return state;
}

static int RequestMystery(bool activate, unsigned eventId, int *states,
                          unsigned capacity, int timeoutMs, unsigned *count)
{
    struct timespec deadline;
    *count = 0;
    pthread_mutex_lock(&sLock);
    if (!sStarted || sState == CTR_HOST_EXITING || !CtrMystery_Query || !CtrMystery_Activate)
    {
        pthread_mutex_unlock(&sLock);
        return CTR_MYSTERY_RESULT_NO_GAME;
    }
    if (sState != CTR_HOST_PAUSED || sMysteryPhase != MYSTERY_IDLE)
    {
        pthread_mutex_unlock(&sLock);
        return CTR_MYSTERY_RESULT_BUSY;
    }
    pthread_once(&sMysteryOnce, InitMysteryCond);
    if (!sMysteryCondReady)
    {
        pthread_mutex_unlock(&sLock);
        return CTR_MYSTERY_RESULT_FAILED;
    }
    sMysteryActivate = activate;
    sMysteryEvent = eventId;
    sMysteryCount = 0;
    sMysteryResult = CTR_MYSTERY_RESULT_FAILED;
    sMysteryPhase = MYSTERY_QUEUED;
    /* RTC/date changes must not extend a queued operation's lifetime. */
    int waitMs = timeoutMs < 0 ? 0 : timeoutMs > 5000 ? 5000 : timeoutMs;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += waitMs / 1000;
    deadline.tv_nsec += (long)(waitMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    sMysteryDeadline = deadline;
    pthread_cond_broadcast(&sCond);
    while (sMysteryPhase != MYSTERY_DONE)
    {
        if (sMysteryPhase == MYSTERY_EXECUTING)
        {
            /* A started in-memory operation cannot be rolled back or reported
             * as unexecuted. It never waits on I/O/UI, and its result is exact. */
            pthread_cond_wait(&sMysteryCond, &sLock);
        }
        else if (pthread_cond_timedwait(&sMysteryCond, &sLock, &deadline) == ETIMEDOUT
                 && sMysteryPhase == MYSTERY_QUEUED)
        {
            sMysteryPhase = MYSTERY_IDLE;
            pthread_mutex_unlock(&sLock);
            return CTR_MYSTERY_RESULT_TIMEOUT;
        }
    }
    int result = sMysteryResult;
    *count = sMysteryCount;
    if (states && capacity && sMysteryCount)
    {
        unsigned n = capacity < sMysteryCount ? capacity : sMysteryCount;
        memcpy(states, sMysteryStates, n * sizeof(*states));
    }
    sMysteryPhase = MYSTERY_IDLE;
    pthread_mutex_unlock(&sLock);
    return result;
}

unsigned CtrHost_QueryMysteryEvents(int *states, unsigned capacity, int timeoutMs)
{
    unsigned count;
    int result = RequestMystery(false, 0, states, capacity, timeoutMs, &count);
    return result == CTR_MYSTERY_RESULT_ACTIVATED ? count : 0;
}

int CtrHost_ActivateMysteryEvent(unsigned eventId, int timeoutMs)
{
    unsigned count;
    if (eventId >= CTR_MYSTERY_MAX_EVENTS) return CTR_MYSTERY_RESULT_INVALID;
    return RequestMystery(true, eventId, NULL, 0, timeoutMs, &count);
}

/* Called with sLock held, exclusively from the game thread's pause loop. */
static void RunMysteryRequest(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > sMysteryDeadline.tv_sec
        || (now.tv_sec == sMysteryDeadline.tv_sec && now.tv_nsec >= sMysteryDeadline.tv_nsec))
    {
        sMysteryResult = CTR_MYSTERY_RESULT_TIMEOUT;
        sMysteryCount = 0;
        sMysteryPhase = MYSTERY_DONE;
        pthread_cond_broadcast(&sMysteryCond);
        return;
    }
    int states[CTR_MYSTERY_MAX_EVENTS];
    for (unsigned i = 0; i < CTR_MYSTERY_MAX_EVENTS; ++i) states[i] = CTR_MYSTERY_NO_GAME;
    unsigned count = 0, eventId = sMysteryEvent;
    bool activate = sMysteryActivate;
    sMysteryPhase = MYSTERY_EXECUTING;
    sPauseAcknowledged = false;
    pthread_mutex_unlock(&sLock);
    int result;
    if (activate)
        result = CtrMystery_Activate(eventId);
    else
    {
        count = CtrMystery_Query(states, CTR_MYSTERY_MAX_EVENTS);
        result = count <= CTR_MYSTERY_MAX_EVENTS ? CTR_MYSTERY_RESULT_ACTIVATED
                                               : CTR_MYSTERY_RESULT_FAILED;
        if (count > CTR_MYSTERY_MAX_EVENTS) count = 0;
    }
    pthread_mutex_lock(&sLock);
    if (count) memcpy(sMysteryStates, states, count * sizeof(*states));
    sMysteryCount = count;
    sMysteryResult = result;
    sMysteryPhase = MYSTERY_DONE;
    sPauseAcknowledged = sState == CTR_HOST_PAUSED;
    pthread_cond_broadcast(&sCond);
    pthread_cond_broadcast(&sMysteryCond);
}

CtrHostState CtrHost_WaitWhilePaused(void)
{
    CtrHostState state;

    pthread_mutex_lock(&sLock);
    sPauseAcknowledged = sState == CTR_HOST_PAUSED;
    pthread_cond_broadcast(&sCond);
    while (sState == CTR_HOST_PAUSED)
    {
        if (sMysteryPhase == MYSTERY_QUEUED)
            RunMysteryRequest();
        else
            pthread_cond_wait(&sCond, &sLock);
    }
    sPauseAcknowledged = false;
    state = sState;
    pthread_mutex_unlock(&sLock);
    return state;
}

bool CtrHost_WaitUntilPaused(int timeoutMs)
{
    struct timespec deadline;
    bool ready;

    pthread_mutex_lock(&sLock);
    DeadlineAfterMs(&deadline, timeoutMs > 0 ? timeoutMs : 0);
    while (sStarted && sState == CTR_HOST_PAUSED && !sPauseAcknowledged)
    {
        if (pthread_cond_timedwait(&sCond, &sLock, &deadline) == ETIMEDOUT)
            break;
    }
    ready = !sStarted || (sState == CTR_HOST_PAUSED && sPauseAcknowledged);
    pthread_mutex_unlock(&sLock);
    return ready;
}

static void *GameThread(void *arg)
{
    (void)arg;
    pthread_setname_np(pthread_self(), "emerald-game");
    /* A returned main has the same cleanup semantics as exit on 3DS. */
    int status = main();
    if (CtrShim_Exit)
        CtrShim_Exit(status);
    CtrHost_NotifyGameExit(status);
    return NULL;
}

bool CtrHost_StartGame(void)
{
    pthread_attr_t attr;
    bool ok;

    pthread_mutex_lock(&sLock);
    if (sStarted)
    {
        pthread_mutex_unlock(&sLock);
        return true;
    }
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GAME_THREAD_STACK);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ok = pthread_create(&sGameThread, &attr, GameThread, NULL) == 0;
    pthread_attr_destroy(&attr);
    sStarted = ok;
    pthread_mutex_unlock(&sLock);
    if (!ok)
        CtrHostJni_Log(ANDROID_LOG_ERROR, "could not create the game thread");
    return ok;
}

bool CtrHost_GameStarted(void)
{
    bool started;

    pthread_mutex_lock(&sLock);
    started = sStarted;
    pthread_mutex_unlock(&sLock);
    return started;
}
