/*
 * Syscalls and the libctru internals origin's __system_allocateHeaps
 * override refers to. That override never runs on Android (bionic owns the
 * heap), but it is compiled and linked, and if it were called it would get
 * plausible New 3DS figures and anonymous memory.
 */
#include <3ds/os.h>
#include <3ds/result.h>
#include <3ds/svc.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "ctr_host.h"
#include "shim_internal.h"

char *fake_heap_start, *fake_heap_end;
u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap;
/* 0 means libctru's default (32 MiB); origin defines it as 8 MiB. */
u32 __ctru_linear_heap_size __attribute__((weak)) = 0;

#define RESOURCE_LIMIT_HANDLE 0x0000C0DEu
/* What a New 3DS application with the default memory mode has to commit. */
#define NEW3DS_APP_MEMORY (124u * 1024u * 1024u)

void CtrShim_Log(int priority, const char *text)
{
    __android_log_write(priority, SHIM_LOG_TAG, text);
}

void ShimLogf(int priority, const char *format, ...)
{
    char text[512];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    __android_log_write(priority, SHIM_LOG_TAG, text);
}

u64 svcGetSystemTick(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * SYSCLOCK_ARM11 + (u64)ts.tv_nsec * SYSCLOCK_ARM11 / 1000000000ull;
}

void svcSleepThread(s64 ns)
{
    struct timespec ts;

    if (ns <= 0)
    {
        sched_yield();
        return;
    }
    ts.tv_sec = (time_t)(ns / 1000000000);
    ts.tv_nsec = (long)(ns % 1000000000);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

Result svcOutputDebugString(const char *str, s32 length)
{
    char line[1024];
    int priority = ANDROID_LOG_INFO;
    size_t n;

    if (str == NULL || length <= 0)
        return 0;
    n = (size_t)length < sizeof(line) - 1 ? (size_t)length : sizeof(line) - 1;
    memcpy(line, str, n);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        --n;
    line[n] = '\0';
    if (strncmp(line, "[ERROR]", 7) == 0)
        priority = ANDROID_LOG_ERROR;
    __android_log_write(priority, SHIM_LOG_TAG, line);
    return 0;
}

void svcBreak(UserBreakType breakReason)
{
    ShimLogf(ANDROID_LOG_FATAL, "svcBreak(%d): the game stopped itself", (int)breakReason);
    CtrHost_NotifyGameExit(-1);
    pthread_exit(NULL);
}

Result svcCloseHandle(Handle handle)
{
    (void)handle;
    return 0;
}

Result svcGetResourceLimit(Handle *resourceLimit, Handle process)
{
    if (process != CUR_PROCESS_HANDLE)
        return MAKERESULT(RL_PERMANENT, RS_WRONGARG, RM_KERNEL, RD_INVALID_HANDLE);
    *resourceLimit = RESOURCE_LIMIT_HANDLE;
    return 0;
}

static Result ResourceLimitValues(s64 *values, Handle resourceLimit, ResourceLimitType *names, s32 nameCount,
                                  bool current)
{
    if (resourceLimit != RESOURCE_LIMIT_HANDLE)
        return MAKERESULT(RL_PERMANENT, RS_WRONGARG, RM_KERNEL, RD_INVALID_HANDLE);
    for (s32 i = 0; i < nameCount; ++i)
    {
        switch (names[i])
        {
        case RESLIMIT_PRIORITY: values[i] = current ? 0 : 0x18; break;
        case RESLIMIT_COMMIT: values[i] = current ? 0 : NEW3DS_APP_MEMORY; break;
        case RESLIMIT_THREAD: values[i] = current ? 1 : 32; break;
        case RESLIMIT_CPUTIME: values[i] = current ? 0 : 80; break;
        default: values[i] = current ? 0 : 32; break;
        }
    }
    return 0;
}

Result svcGetResourceLimitLimitValues(s64 *values, Handle resourceLimit, ResourceLimitType *names, s32 nameCount)
{
    return ResourceLimitValues(values, resourceLimit, names, nameCount, false);
}

Result svcGetResourceLimitCurrentValues(s64 *values, Handle resourceLimit, ResourceLimitType *names,
                                        s32 nameCount)
{
    return ResourceLimitValues(values, resourceLimit, names, nameCount, true);
}

Result svcControlMemory(u32 *addr_out, u32 addr0, u32 addr1, u32 size, MemOp op, MemPerm perm)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    void *memory;

    (void)addr1;
    (void)perm;
    switch (op & MEMOP_OP_MASK)
    {
    case MEMOP_ALLOC:
#if defined(__x86_64__)
        flags |= MAP_32BIT; /* host tests: the address must fit in a u32 */
#endif
        memory = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (memory == MAP_FAILED)
            return MAKERESULT(RL_PERMANENT, RS_OUTOFRESOURCE, RM_KERNEL, RD_OUT_OF_MEMORY);
        if (addr_out)
            *addr_out = (u32)(uintptr_t)memory;
        return 0;
    case MEMOP_FREE:
        munmap((void *)(uintptr_t)addr0, size);
        return 0;
    default:
        return MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_KERNEL, RD_NOT_IMPLEMENTED);
    }
}

u64 osGetTime(void)
{
    /* 1900-01-01 to 1970-01-01, in milliseconds. */
    const u64 epochDelta = 2208988800ull * 1000ull;
    struct timeval tv;
    struct tm local;
    time_t now;

    gettimeofday(&tv, NULL);
    now = tv.tv_sec;
    localtime_r(&now, &local);
    return epochDelta + ((u64)now + (s64)local.tm_gmtoff) * 1000ull + (u64)tv.tv_usec / 1000ull;
}

void osSetSpeedupEnable(bool enable)
{
    (void)enable;
}
