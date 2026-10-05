/*
 * APT: the activity lifecycle (ctr_host.h) seen through aptMainLoop. See
 * 3ds/services/apt.h and ctrshim_apt.h for the order of hooks and listeners.
 */
#include <3ds/services/apt.h>
#include <3ds/result.h>
#include <pthread.h>
#include <stdatomic.h>

#include "ctr_host.h"
#include "ctrshim_apt.h"
#include "shim_internal.h"

#define MAX_LISTENERS 8

typedef struct
{
    CtrAptListener listener;
    void *user;
} Listener;

static aptHookCookie *sHooks;
static bool sExited, sSleepAllowed = true;
static volatile bool sSuspended;
static atomic_uint sCpuTimeHint;

static pthread_mutex_t sListenerLock = PTHREAD_MUTEX_INITIALIZER;
static Listener sListeners[MAX_LISTENERS];
static int sListenerCount;

Result aptInit(void)
{
    return 0;
}

void aptExit(void)
{
}

bool aptIsActive(void)
{
    return !sSuspended && !sExited;
}

bool aptShouldClose(void)
{
    return sExited || CtrHost_GetState() == CTR_HOST_EXITING;
}

void aptSetSleepAllowed(bool allowed)
{
    sSleepAllowed = allowed;
}

bool aptIsSleepAllowed(void)
{
    return sSleepAllowed;
}

Result APT_CheckNew3DS(bool *out)
{
    if (out)
        *out = true;
    return 0;
}

Result APT_SetAppCpuTimeLimit(u32 percent)
{
    /* Upstream requests a share of the 3DS system core before creating its
     * asset-streaming worker. Android's scheduler already runs those pthreads
     * across available cores; there is no separate reserved system core. */
    if (percent > 100)
        return MAKERESULT(RL_USAGE, RS_INVALIDARG, RM_APT, RD_OUT_OF_RANGE);
    atomic_store(&sCpuTimeHint, percent);
    return 0;
}

Result APT_GetAppCpuTimeLimit(u32 *percent)
{
    if (!percent)
        return MAKERESULT(RL_USAGE, RS_INVALIDARG, RM_APT, RD_INVALID_POINTER);
    *percent = atomic_load(&sCpuTimeHint);
    return 0;
}

void aptHook(aptHookCookie *cookie, aptHookFn callback, void *param)
{
    if (cookie == NULL || callback == NULL)
        return;
    cookie->callback = callback;
    cookie->param = param;
    cookie->next = sHooks;
    sHooks = cookie;
}

void aptUnhook(aptHookCookie *cookie)
{
    for (aptHookCookie **link = &sHooks; *link != NULL; link = &(*link)->next)
    {
        if (*link == cookie)
        {
            *link = cookie->next;
            cookie->next = NULL;
            return;
        }
    }
}

static void CallHooks(APT_HookType type)
{
    aptHookCookie *next;

    /* A hook may unhook itself. */
    for (aptHookCookie *hook = sHooks; hook != NULL; hook = next)
    {
        next = hook->next;
        hook->callback(type, hook->param);
    }
}

bool CtrApt_AddListener(CtrAptListener listener, void *user)
{
    bool added = false;

    if (listener == NULL)
        return false;
    pthread_mutex_lock(&sListenerLock);
    for (int i = 0; i < sListenerCount; ++i)
        if (sListeners[i].listener == listener && sListeners[i].user == user)
            goto out;
    if (sListenerCount < MAX_LISTENERS)
    {
        sListeners[sListenerCount].listener = listener;
        sListeners[sListenerCount].user = user;
        ++sListenerCount;
        added = true;
    }
out:
    pthread_mutex_unlock(&sListenerLock);
    return added;
}

void CtrApt_RemoveListener(CtrAptListener listener, void *user)
{
    pthread_mutex_lock(&sListenerLock);
    for (int i = 0; i < sListenerCount; ++i)
    {
        if (sListeners[i].listener == listener && sListeners[i].user == user)
        {
            for (int j = i + 1; j < sListenerCount; ++j)
                sListeners[j - 1] = sListeners[j];
            --sListenerCount;
            break;
        }
    }
    pthread_mutex_unlock(&sListenerLock);
}

bool CtrApt_IsSuspended(void)
{
    return sSuspended;
}

static void Notify(CtrAptEvent event)
{
    Listener copy[MAX_LISTENERS];
    int count;

    pthread_mutex_lock(&sListenerLock);
    count = sListenerCount;
    for (int i = 0; i < count; ++i)
        copy[i] = sListeners[i];
    pthread_mutex_unlock(&sListenerLock);
    if (event == CTR_APT_RESUME)
    {
        for (int i = 0; i < count; ++i)
            copy[i].listener(event, copy[i].user);
    }
    else
    {
        for (int i = count - 1; i >= 0; --i)
            copy[i].listener(event, copy[i].user);
    }
}

static bool Exit(void)
{
    sExited = true;
    ShimLogf(ANDROID_LOG_INFO, "APT: exit");
    CallHooks(APTHOOK_ONEXIT);
    Notify(CTR_APT_EXIT);
    return false;
}

bool aptMainLoop(void)
{
    if (sExited)
        return false;
    switch (CtrHost_GetState())
    {
    case CTR_HOST_RUNNING:
        return true;
    case CTR_HOST_PAUSED:
        ShimLogf(ANDROID_LOG_INFO, "APT: suspend");
        CallHooks(APTHOOK_ONSUSPEND);
        sSuspended = true;
        Notify(CTR_APT_SUSPEND);
        if (CtrHost_WaitWhilePaused() == CTR_HOST_EXITING)
            return Exit();
        sSuspended = false;
        ShimLogf(ANDROID_LOG_INFO, "APT: restore");
        Notify(CTR_APT_RESUME);
        CallHooks(APTHOOK_ONRESTORE);
        return true;
    case CTR_HOST_EXITING:
    default:
        return Exit();
    }
}
