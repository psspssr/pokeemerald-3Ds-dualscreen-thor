/*
 * libctru threads on pthreads. Every pthread is created detached; joining is
 * a futex on the thread's finished flag, which gives threadJoin its timeout.
 * A detached Thread frees itself when it ends; a joinable one is freed by
 * threadFree once finished, as in libctru.
 */
#include <3ds/thread.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/resource.h>

#include "shim_internal.h"

#define DEFAULT_PRIORITY 0x30

struct Thread_tag
{
    ThreadFunc entry;
    void *arg;
    int priority;
    int creatorPriority;
    bool detached;
    /* 0 running, 1 finished: the join futex. */
    s32 finished;
    int exitCode;
};

/* detached/finished hand-over between the thread and threadDetach. */
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static __thread Thread tCurrent;
static __thread int tPriority = DEFAULT_PRIORITY;
static __thread bool tPrioritySet;

static int CurrentPriority(void)
{
    return tPrioritySet ? tPriority : DEFAULT_PRIORITY;
}

/* Lower 3DS priority (a larger number) than the creator: a larger nice. */
static void ApplyPriority(int priority, int relativeTo)
{
    int delta = priority - relativeTo;
    int nice;

    if (delta <= 0)
        return;
    errno = 0;
    nice = getpriority(PRIO_PROCESS, 0);
    if (nice == -1 && errno != 0)
        return;
    nice += delta;
    setpriority(PRIO_PROCESS, 0, nice > 19 ? 19 : nice);
}

static void Finish(Thread thread, int rc)
{
    bool detached;

    pthread_mutex_lock(&sLock);
    thread->exitCode = rc;
    detached = thread->detached;
    if (!detached)
    {
        __atomic_store_n(&thread->finished, 1, __ATOMIC_RELEASE);
        ShimFutexWake(&thread->finished, INT_MAX);
    }
    pthread_mutex_unlock(&sLock);
    if (detached)
        free(thread);
}

static void *Start(void *arg)
{
    Thread thread = arg;

    tCurrent = thread;
    tPriority = thread->priority;
    tPrioritySet = true;
    ApplyPriority(thread->priority, thread->creatorPriority);
    thread->entry(thread->arg);
    Finish(thread, 0);
    return NULL;
}

Thread threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size, int prio, int core_id, bool detached)
{
    pthread_attr_t attr;
    pthread_t handle;
    Thread thread;
    size_t stack;
    int rc;

    /* svcCreateThread's checks. */
    if (entrypoint == NULL || prio < 0x18 || prio > 0x3F || core_id < -2 || core_id > 3)
        return NULL;
    thread = calloc(1, sizeof(*thread));
    if (thread == NULL)
        return NULL;
    thread->entry = entrypoint;
    thread->arg = arg;
    thread->priority = prio;
    thread->creatorPriority = CurrentPriority();
    thread->detached = detached;

    stack = stack_size < CTR_THREAD_MIN_STACK ? CTR_THREAD_MIN_STACK : stack_size;
    stack = (stack + 4095u) & ~(size_t)4095u;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(&handle, &attr, Start, thread);
    pthread_attr_destroy(&attr);
    if (rc != 0)
    {
        ShimLogf(ANDROID_LOG_ERROR, "threadCreate: pthread_create failed (%d)", rc);
        free(thread);
        return NULL;
    }
    return thread;
}

Handle threadGetHandle(Thread thread)
{
    return thread == tCurrent ? CUR_THREAD_HANDLE : (Handle)(uintptr_t)thread;
}

int threadGetExitCode(Thread thread)
{
    if (thread == NULL || !__atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE))
        return 0;
    return thread->exitCode;
}

void threadFree(Thread thread)
{
    if (thread == NULL || !__atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE))
        return;
    /* The thread may still be inside Finish, after its last store. */
    pthread_mutex_lock(&sLock);
    pthread_mutex_unlock(&sLock);
    free(thread);
}

Result threadJoin(Thread thread, u64 timeout_ns)
{
    struct timespec deadline;
    const struct timespec *until;

    if (thread == NULL)
        return 0;
    until = timeout_ns != U64_MAX && ShimDeadline((s64)(timeout_ns > INT64_MAX ? INT64_MAX : timeout_ns), &deadline)
                ? &deadline
                : NULL;
    while (!__atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE))
    {
        if (ShimFutexWait(&thread->finished, 0, until) == ETIMEDOUT)
            return __atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE) ? 0 : CTR_RESULT_TIMEOUT;
    }
    return 0;
}

void threadDetach(Thread thread)
{
    bool finished;

    if (thread == NULL)
        return;
    pthread_mutex_lock(&sLock);
    finished = __atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE);
    thread->detached = true;
    pthread_mutex_unlock(&sLock);
    if (finished)
        free(thread);
}

Thread threadGetCurrent(void)
{
    return tCurrent;
}

void threadExit(int rc)
{
    Thread thread = tCurrent;

    if (thread != NULL)
        Finish(thread, rc);
    pthread_exit(NULL);
}

Result svcGetThreadPriority(s32 *out, Handle handle)
{
    if (handle == CUR_THREAD_HANDLE)
    {
        *out = CurrentPriority();
        return 0;
    }
    return MAKERESULT(RL_PERMANENT, RS_WRONGARG, RM_KERNEL, RD_INVALID_HANDLE);
}

Result svcSetThreadPriority(Handle handle, s32 prio)
{
    if (prio < 0x18 || prio > 0x3F)
        return MAKERESULT(RL_USAGE, RS_INVALIDARG, RM_KERNEL, RD_OUT_OF_RANGE);
    if (handle == CUR_THREAD_HANDLE)
    {
        ApplyPriority(prio, CurrentPriority());
        tPriority = prio;
        tPrioritySet = true;
        return 0;
    }
    return MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_KERNEL, RD_NOT_IMPLEMENTED);
}
