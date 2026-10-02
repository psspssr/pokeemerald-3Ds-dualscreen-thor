#ifndef CTRSHIM_H
#define CTRSHIM_H

/*
 * The shim's process-level services for the host (android/host) and the
 * GPU emulation (android/gpu). Nothing here is part of libctru.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What exit() does on the game thread (libemerald.so is linked with
 * --wrap=exit, see wrap.txt): the handlers registered with atexit() run,
 * newest first, stdio is flushed, CtrHost_NotifyGameExit(status) is called
 * and the calling thread ends with pthread_exit. The process is never
 * terminated from native code. Returning from main() on a 3DS is exit(), so
 * the game thread should end with CtrShim_Exit(main()).
 */
void CtrShim_Exit(int status) __attribute__((noreturn));

/*
 * "romfs:/x" or "romfs:x" -> CtrHost_RomfsDir()/x, "sdmc:/x" or "sdmc:x" ->
 * CtrHost_SdmcDir()/x; any other path is returned unchanged (no copy). NULL
 * with errno set when the path cannot be mapped: ENODEV for romfs: while the
 * RomFS is not mounted, ENAMETOOLONG when it does not fit in size bytes.
 * Every wrapped libc file function (wrap.txt) maps its path arguments with
 * this, so code calling those functions needs nothing else.
 */
const char *CtrShim_MapPath(const char *path, char *buffer, size_t size);

/* Writes text to logcat (tag "Emerald3DS") at the given android_LogPriority. */
void CtrShim_Log(int priority, const char *text);

#ifdef __cplusplus
}
#endif

#endif
