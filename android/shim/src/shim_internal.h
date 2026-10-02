#ifndef SHIM_INTERNAL_H
#define SHIM_INTERNAL_H

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <android/log.h>

#include "ctrshim.h"

#define SHIM_LOG_TAG "Emerald3DS"

void ShimLogf(int priority, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* hid.c: the KEY_CPAD_* bits for a circle pad position. */
uint32_t CtrHid_CirclePadKeys(int x, int y);

static inline uint64_t ShimNowNs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* An absolute CLOCK_MONOTONIC deadline timeout_ns from now; false if the
 * timeout is "forever" (negative, or too large to matter). */
static inline bool ShimDeadline(int64_t timeout_ns, struct timespec *out)
{
    uint64_t at;

    if (timeout_ns < 0 || (uint64_t)timeout_ns > 100ull * 365 * 24 * 3600 * 1000000000ull)
        return false;
    at = ShimNowNs() + (uint64_t)timeout_ns;
    out->tv_sec = (time_t)(at / 1000000000ull);
    out->tv_nsec = (long)(at % 1000000000ull);
    return true;
}

/*
 * Sleeps while *addr == expected, until woken or the absolute monotonic
 * deadline (NULL: none). 0 when woken, EAGAIN if *addr differed, ETIMEDOUT,
 * or EINTR. Spurious returns are possible; callers recheck their condition.
 */
static inline int ShimFutexWait(volatile int32_t *addr, int32_t expected, const struct timespec *deadline)
{
    long rc = syscall(SYS_futex, (int32_t *)addr, FUTEX_WAIT_BITSET_PRIVATE, expected, deadline, NULL,
                      FUTEX_BITSET_MATCH_ANY);
    return rc == 0 ? 0 : errno;
}

static inline void ShimFutexWake(volatile int32_t *addr, int count)
{
    syscall(SYS_futex, (int32_t *)addr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
}

#endif
