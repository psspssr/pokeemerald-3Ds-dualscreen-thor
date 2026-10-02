/*
 * libctru's light synchronization primitives on futexes. The layouts are
 * libctru's (3ds/synchronization.h) and so are the encodings that matter to
 * callers: a LightLock word >= 0 is unlocked (0 being a zeroed, never
 * initialised lock), and LightEvent states are libctru's four values.
 */
#include <3ds/synchronization.h>

#include "shim_internal.h"

enum
{
    CLEARED_STICKY = -2,
    CLEARED_ONESHOT = -1,
    SIGNALED_ONESHOT = 0,
    SIGNALED_STICKY = 1,
};

/* LightLock: >= 0 unlocked, -1 locked, -2 locked with (possible) waiters. */
void LightLock_Init(LightLock *lock)
{
    __atomic_store_n(lock, 1, __ATOMIC_RELEASE);
}

void LightLock_Lock(LightLock *lock)
{
    s32 value = __atomic_load_n(lock, __ATOMIC_RELAXED);

    if (value >= 0 && __atomic_compare_exchange_n(lock, &value, -1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;
    while (__atomic_exchange_n(lock, -2, __ATOMIC_ACQUIRE) < 0)
        ShimFutexWait(lock, -2, NULL);
}

int LightLock_TryLock(LightLock *lock)
{
    s32 value = __atomic_load_n(lock, __ATOMIC_RELAXED);

    if (value >= 0 && __atomic_compare_exchange_n(lock, &value, -1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    return 1;
}

void LightLock_Unlock(LightLock *lock)
{
    if (__atomic_exchange_n(lock, 1, __ATOMIC_RELEASE) == -2)
        ShimFutexWake(lock, 1);
}

static u32 ThreadTag(void)
{
    static __thread u32 tag;

    if (tag == 0)
        tag = (u32)syscall(SYS_gettid);
    return tag;
}

void RecursiveLock_Init(RecursiveLock *lock)
{
    LightLock_Init(&lock->lock);
    lock->thread_tag = 0;
    lock->counter = 0;
}

void RecursiveLock_Lock(RecursiveLock *lock)
{
    u32 tag = ThreadTag();

    if (__atomic_load_n(&lock->thread_tag, __ATOMIC_RELAXED) != tag)
    {
        LightLock_Lock(&lock->lock);
        __atomic_store_n(&lock->thread_tag, tag, __ATOMIC_RELAXED);
    }
    lock->counter++;
}

int RecursiveLock_TryLock(RecursiveLock *lock)
{
    u32 tag = ThreadTag();

    if (__atomic_load_n(&lock->thread_tag, __ATOMIC_RELAXED) != tag)
    {
        if (LightLock_TryLock(&lock->lock))
            return 1;
        __atomic_store_n(&lock->thread_tag, tag, __ATOMIC_RELAXED);
    }
    lock->counter++;
    return 0;
}

void RecursiveLock_Unlock(RecursiveLock *lock)
{
    if (--lock->counter == 0)
    {
        __atomic_store_n(&lock->thread_tag, 0, __ATOMIC_RELAXED);
        LightLock_Unlock(&lock->lock);
    }
}

/* CondVar: a wake-up sequence number. */
void CondVar_Init(CondVar *cv)
{
    __atomic_store_n(cv, 0, __ATOMIC_RELEASE);
}

static int CondVarWait(CondVar *cv, LightLock *lock, const struct timespec *deadline)
{
    s32 seq = __atomic_load_n(cv, __ATOMIC_ACQUIRE);
    int rc;

    LightLock_Unlock(lock);
    rc = ShimFutexWait(cv, seq, deadline);
    LightLock_Lock(lock);
    return rc == ETIMEDOUT;
}

void CondVar_Wait(CondVar *cv, LightLock *lock)
{
    CondVarWait(cv, lock, NULL);
}

int CondVar_WaitTimeout(CondVar *cv, LightLock *lock, s64 timeout_ns)
{
    struct timespec deadline;

    return CondVarWait(cv, lock, ShimDeadline(timeout_ns, &deadline) ? &deadline : NULL);
}

void CondVar_WakeUp(CondVar *cv, s32 num_threads)
{
    __atomic_add_fetch(cv, 1, __ATOMIC_RELEASE);
    ShimFutexWake(cv, num_threads < 0 ? INT_MAX : num_threads);
}

void LightEvent_Init(LightEvent *event, ResetType reset_type)
{
    LightLock_Init(&event->lock);
    __atomic_store_n(&event->state, reset_type == RESET_STICKY ? CLEARED_STICKY : CLEARED_ONESHOT,
                     __ATOMIC_RELEASE);
}

void LightEvent_Clear(LightEvent *event)
{
    s32 state = __atomic_load_n(&event->state, __ATOMIC_ACQUIRE);

    if (state == SIGNALED_STICKY)
        __atomic_store_n(&event->state, CLEARED_STICKY, __ATOMIC_RELEASE);
    else if (state == SIGNALED_ONESHOT)
        __atomic_store_n(&event->state, CLEARED_ONESHOT, __ATOMIC_RELEASE);
}

/* As in libctru: a cleared sticky event releases every waiter without
 * changing state; on a cleared one-shot event a waiter is woken only to find
 * it still cleared; a signalled event is cleared. */
void LightEvent_Pulse(LightEvent *event)
{
    s32 state = __atomic_load_n(&event->state, __ATOMIC_ACQUIRE);

    if (state == CLEARED_STICKY)
        ShimFutexWake(&event->state, INT_MAX);
    else if (state == CLEARED_ONESHOT)
        ShimFutexWake(&event->state, 1);
    else
        LightEvent_Clear(event);
}

void LightEvent_Signal(LightEvent *event)
{
    s32 expected = CLEARED_ONESHOT;

    if (__atomic_compare_exchange_n(&event->state, &expected, SIGNALED_ONESHOT, false, __ATOMIC_RELEASE,
                                    __ATOMIC_RELAXED))
    {
        ShimFutexWake(&event->state, 1);
        return;
    }
    expected = CLEARED_STICKY;
    if (__atomic_compare_exchange_n(&event->state, &expected, SIGNALED_STICKY, false, __ATOMIC_RELEASE,
                                    __ATOMIC_RELAXED))
        ShimFutexWake(&event->state, INT_MAX);
}

static bool TryReset(LightEvent *event)
{
    s32 expected = SIGNALED_ONESHOT;

    return __atomic_compare_exchange_n(&event->state, &expected, CLEARED_ONESHOT, false, __ATOMIC_ACQUIRE,
                                       __ATOMIC_RELAXED);
}

int LightEvent_TryWait(LightEvent *event)
{
    if (__atomic_load_n(&event->state, __ATOMIC_ACQUIRE) == SIGNALED_STICKY)
        return 1;
    return TryReset(event);
}

/* Zero once signalled, non-zero on timeout. */
static int EventWait(LightEvent *event, const struct timespec *deadline)
{
    for (;;)
    {
        s32 state = __atomic_load_n(&event->state, __ATOMIC_ACQUIRE);
        int rc;

        if (state == SIGNALED_STICKY)
            return 0;
        if (state == SIGNALED_ONESHOT)
        {
            if (TryReset(event))
                return 0;
            continue;
        }
        rc = ShimFutexWait(&event->state, state, deadline);
        if (rc == ETIMEDOUT)
            return 1;
        /* libctru returns from a cleared sticky wait on any wake (Pulse). */
        if (rc == 0 && state == CLEARED_STICKY)
            return 0;
    }
}

void LightEvent_Wait(LightEvent *event)
{
    EventWait(event, NULL);
}

int LightEvent_WaitTimeout(LightEvent *event, s64 timeout_ns)
{
    struct timespec deadline;

    return EventWait(event, ShimDeadline(timeout_ns, &deadline) ? &deadline : NULL);
}

void LightSemaphore_Init(LightSemaphore *semaphore, s16 initial_count, s16 max_count)
{
    semaphore->num_threads_acq = 0;
    semaphore->max_count = max_count;
    __atomic_store_n(&semaphore->current_count, initial_count, __ATOMIC_RELEASE);
}

int LightSemaphore_TryAcquire(LightSemaphore *semaphore, s32 count)
{
    s32 current = __atomic_load_n(&semaphore->current_count, __ATOMIC_RELAXED);

    while (current >= count)
        if (__atomic_compare_exchange_n(&semaphore->current_count, &current, current - count, true,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
    return 1;
}

void LightSemaphore_Acquire(LightSemaphore *semaphore, s32 count)
{
    while (LightSemaphore_TryAcquire(semaphore, count))
    {
        s32 current = __atomic_load_n(&semaphore->current_count, __ATOMIC_RELAXED);

        if (current < count)
        {
            __atomic_add_fetch(&semaphore->num_threads_acq, 1, __ATOMIC_RELAXED);
            ShimFutexWait(&semaphore->current_count, current, NULL);
            __atomic_sub_fetch(&semaphore->num_threads_acq, 1, __ATOMIC_RELAXED);
        }
    }
}

void LightSemaphore_Release(LightSemaphore *semaphore, s32 count)
{
    __atomic_add_fetch(&semaphore->current_count, count, __ATOMIC_RELEASE);
    ShimFutexWake(&semaphore->current_count, INT_MAX);
}
