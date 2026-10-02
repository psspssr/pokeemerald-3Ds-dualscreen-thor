/*
 * exit() and atexit() for the game thread (see CtrShim_Exit in ctrshim.h).
 * The game's atexit handlers are kept here rather than given to bionic,
 * which would run them only when the whole process exits - if at all.
 */
#include <pthread.h>
#include <stdio.h>

#include "ctr_host.h"
#include "shim_internal.h"

/* What C guarantees; origin registers one. */
#define MAX_HANDLERS 32

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static void (*sHandlers[MAX_HANDLERS])(void);
static int sCount;

int __wrap_atexit(void (*function)(void))
{
    int rc = -1;

    pthread_mutex_lock(&sLock);
    if (function != NULL && sCount < MAX_HANDLERS)
    {
        sHandlers[sCount++] = function;
        rc = 0;
    }
    pthread_mutex_unlock(&sLock);
    return rc;
}

void CtrShim_Exit(int status)
{
    /* Popped one at a time, so a handler that calls exit() itself still
     * leaves the remaining handlers to run exactly once. */
    for (;;)
    {
        void (*function)(void) = NULL;

        pthread_mutex_lock(&sLock);
        if (sCount > 0)
            function = sHandlers[--sCount];
        pthread_mutex_unlock(&sLock);
        if (function == NULL)
            break;
        function();
    }
    fflush(NULL);
    ShimLogf(ANDROID_LOG_INFO, "exit(%d)", status);
    CtrHost_NotifyGameExit(status);
    pthread_exit(NULL);
}

void __wrap_exit(int status) __attribute__((noreturn));
void __wrap_exit(int status)
{
    CtrShim_Exit(status);
}
