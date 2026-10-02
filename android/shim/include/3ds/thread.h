/**
 * @file thread.h
 * @brief libctru's threads (zlib licence, devkitPro), on pthreads.
 *
 * Priorities follow the 3DS (0x18 highest .. 0x3F lowest) and are checked
 * like svcCreateThread does. A thread made lower in priority than its
 * creator gets a correspondingly higher nice value; raising is not possible
 * for an app and is ignored. The core id is checked and otherwise ignored.
 * Stacks are at least CTR_THREAD_MIN_STACK: bionic's stdio and the
 * Android linker need far more than the 8-16 KiB 3DS code asks for.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/synchronization.h>
#include <3ds/svc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTR_THREAD_MIN_STACK (256u * 1024u)

typedef struct Thread_tag *Thread;

Thread threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size, int prio, int core_id, bool detached);
Handle threadGetHandle(Thread thread);
int threadGetExitCode(Thread thread);
/* Only frees a finished thread, like libctru. */
void threadFree(Thread thread);
/* U64_MAX waits forever. Returns 0, or CTR_RESULT_TIMEOUT. */
Result threadJoin(Thread thread, u64 timeout_ns);
void threadDetach(Thread thread);
/* NULL for threads the shim did not create (the game's main thread). */
Thread threadGetCurrent(void);
void threadExit(int rc) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
