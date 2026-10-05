/**
 * @file svc.h
 * @brief The subset of libctru's syscall wrappers (zlib licence, devkitPro)
 * that the port uses, implemented on Android by android/shim/src/svc.c.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CUR_PROCESS_HANDLE 0xFFFF8001
#define CUR_THREAD_HANDLE 0xFFFF8000

typedef enum
{
    MEMOP_FREE = 1,
    MEMOP_RESERVE = 2,
    MEMOP_ALLOC = 3,
    MEMOP_MAP = 4,
    MEMOP_UNMAP = 5,
    MEMOP_PROT = 6,

    MEMOP_REGION_APP = 0x100,
    MEMOP_REGION_SYSTEM = 0x200,
    MEMOP_REGION_BASE = 0x300,

    MEMOP_OP_MASK = 0xFF,
    MEMOP_REGION_MASK = 0xF00,
    MEMOP_LINEAR_FLAG = 0x10000,

    MEMOP_ALLOC_LINEAR = MEMOP_LINEAR_FLAG | MEMOP_ALLOC,
} MemOp;

typedef enum
{
    MEMPERM_READ = 1,
    MEMPERM_WRITE = 2,
    MEMPERM_EXECUTE = 4,
    MEMPERM_READWRITE = MEMPERM_READ | MEMPERM_WRITE,
    MEMPERM_READEXECUTE = MEMPERM_READ | MEMPERM_EXECUTE,
    MEMPERM_DONTCARE = 0x10000000,
} MemPerm;

typedef enum
{
    ARBITRATION_SIGNAL = 0,
    ARBITRATION_WAIT_IF_LESS_THAN = 1,
    ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN = 2,
    ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT = 3,
    ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN_TIMEOUT = 4,
} ArbitrationType;

#define ARBITRATION_SIGNAL_ALL (-1)

typedef enum
{
    RESET_ONESHOT = 0,
    RESET_STICKY = 1,
    RESET_PULSE = 2,
} ResetType;

typedef enum
{
    RESLIMIT_PRIORITY = 0,
    RESLIMIT_COMMIT = 1,
    RESLIMIT_THREAD = 2,
    RESLIMIT_EVENT = 3,
    RESLIMIT_MUTEX = 4,
    RESLIMIT_SEMAPHORE = 5,
    RESLIMIT_TIMER = 6,
    RESLIMIT_SHAREDMEMORY = 7,
    RESLIMIT_ADDRESSARBITER = 8,
    RESLIMIT_CPUTIME = 9,
    RESLIMIT_BIT = BIT(31),
} ResourceLimitType;

typedef enum
{
    USERBREAK_PANIC = 0,
    USERBREAK_ASSERT = 1,
    USERBREAK_USER = 2,
    USERBREAK_LOAD_RO = 3,
    USERBREAK_UNLOAD_RO = 4,
} UserBreakType;

/*
 * Ticks of the ARM11 system clock (SYSCLOCK_ARM11 per second), from
 * CLOCK_MONOTONIC: monotonic, and the same unit origin's millisecond maths uses.
 */
u64 svcGetSystemTick(void);
/* No 3DS kernel/custom-firmware extensions are exposed on Android. */
Result svcGetSystemInfo(s64 *out, u32 type, s32 param);
void svcSleepThread(s64 ns);
/* To logcat, tag "Emerald3DS". */
Result svcOutputDebugString(const char *str, s32 length);
/* Logs, tells the host the game failed (CtrHost_NotifyGameExit(-1)) and ends
 * the calling thread. The process is left alone. */
void svcBreak(UserBreakType breakReason) __attribute__((noreturn));
Result svcCloseHandle(Handle handle);
/* Priorities as given to threadCreate; 0x30 for threads the shim did not create. */
Result svcGetThreadPriority(s32 *out, Handle handle);
Result svcSetThreadPriority(Handle thread, s32 prio);

/*
 * Present so libctru-internal code (main_3ds.c's __system_allocateHeaps)
 * compiles and is harmless if run: the limits describe a New 3DS
 * application, and MEMOP_ALLOC maps ordinary anonymous memory wherever the
 * kernel puts it (addr0 is ignored).
 */
Result svcGetResourceLimit(Handle *resourceLimit, Handle process);
Result svcGetResourceLimitLimitValues(s64 *values, Handle resourceLimit, ResourceLimitType *names, s32 nameCount);
Result svcGetResourceLimitCurrentValues(s64 *values, Handle resourceLimit, ResourceLimitType *names, s32 nameCount);
Result svcControlMemory(u32 *addr_out, u32 addr0, u32 addr1, u32 size, MemOp op, MemPerm perm);

#ifdef __cplusplus
}
#endif
