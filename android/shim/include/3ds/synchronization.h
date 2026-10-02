/**
 * @file synchronization.h
 * @brief libctru's light synchronization primitives (zlib licence,
 * devkitPro), for the Android shim.
 *
 * Same types and layouts as libctru, implemented on Linux futexes, so a
 * zero-initialised LightLock is unlocked and a zero-initialised LightEvent
 * is a signalled one-shot event, exactly as on the console. LightLock is not
 * recursive; RecursiveLock is.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/svc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef s32 _LOCK_T;

typedef struct
{
    _LOCK_T lock;
    u32 thread_tag;
    u32 counter;
} _LOCK_RECURSIVE_T;

typedef _LOCK_T LightLock;
typedef _LOCK_RECURSIVE_T RecursiveLock;
typedef s32 CondVar;

typedef struct
{
    /* -2 cleared sticky, -1 cleared oneshot, 0 signalled oneshot, 1 signalled sticky */
    s32 state;
    LightLock lock;
} LightEvent;

typedef struct
{
    s32 current_count;
    s16 num_threads_acq;
    s16 max_count;
} LightSemaphore;

static inline void __dsb(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline void __dmb(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline void __isb(void)
{
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
}

#define AtomicIncrement(ptr) __atomic_add_fetch((u32 *)(ptr), 1, __ATOMIC_SEQ_CST)
#define AtomicDecrement(ptr) __atomic_sub_fetch((u32 *)(ptr), 1, __ATOMIC_SEQ_CST)
#define AtomicPostIncrement(ptr) __atomic_fetch_add((u32 *)(ptr), 1, __ATOMIC_SEQ_CST)
#define AtomicPostDecrement(ptr) __atomic_fetch_sub((u32 *)(ptr), 1, __ATOMIC_SEQ_CST)
#define AtomicSwap(ptr, value) __atomic_exchange_n((u32 *)(ptr), (value), __ATOMIC_SEQ_CST)

void LightLock_Init(LightLock *lock);
void LightLock_Lock(LightLock *lock);
/* Zero on success, non-zero on failure. */
int LightLock_TryLock(LightLock *lock);
void LightLock_Unlock(LightLock *lock);

void RecursiveLock_Init(RecursiveLock *lock);
void RecursiveLock_Lock(RecursiveLock *lock);
int RecursiveLock_TryLock(RecursiveLock *lock);
void RecursiveLock_Unlock(RecursiveLock *lock);

void CondVar_Init(CondVar *cv);
void CondVar_Wait(CondVar *cv, LightLock *lock);
/* Zero on success, non-zero on timeout. */
int CondVar_WaitTimeout(CondVar *cv, LightLock *lock, s64 timeout_ns);
void CondVar_WakeUp(CondVar *cv, s32 num_threads);

static inline void CondVar_Signal(CondVar *cv)
{
    CondVar_WakeUp(cv, 1);
}

static inline void CondVar_Broadcast(CondVar *cv)
{
    CondVar_WakeUp(cv, ARBITRATION_SIGNAL_ALL);
}

void LightEvent_Init(LightEvent *event, ResetType reset_type);
void LightEvent_Clear(LightEvent *event);
void LightEvent_Pulse(LightEvent *event);
void LightEvent_Signal(LightEvent *event);
/* Non-zero if the event was signalled. */
int LightEvent_TryWait(LightEvent *event);
void LightEvent_Wait(LightEvent *event);
/* Non-zero on timeout, zero if the event was signalled. */
int LightEvent_WaitTimeout(LightEvent *event, s64 timeout_ns);

void LightSemaphore_Init(LightSemaphore *semaphore, s16 initial_count, s16 max_count);
void LightSemaphore_Acquire(LightSemaphore *semaphore, s32 count);
/* Zero on success, non-zero on failure. */
int LightSemaphore_TryAcquire(LightSemaphore *semaphore, s32 count);
void LightSemaphore_Release(LightSemaphore *semaphore, s32 count);

#ifdef __cplusplus
}
#endif
