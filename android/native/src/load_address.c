/*
 * libemerald.so only works at its link address: the asset index the build
 * generates holds link-time stub addresses, which 3ds_assets.c compares with
 * live pointers (the 3DSX is always loaded where it was linked).
 * libemeraldboot.so (android/native/boot) loads it there; fail at once,
 * with the reason in logcat, if something else loaded it.
 */

#include <stdint.h>
#include <stdlib.h>

#include <android/log.h>

extern const char __ehdr_start[];

__attribute__((constructor)) static void CheckLoadAddress(void)
{
    if ((uintptr_t)__ehdr_start == (uintptr_t)CTR_LOAD_ADDRESS)
        return;
    __android_log_print(ANDROID_LOG_FATAL, "emerald",
                        "libemerald.so is loaded at %p, not at its link address %p: "
                        "load libemeraldboot.so first",
                        (const void *)__ehdr_start, (const void *)(uintptr_t)CTR_LOAD_ADDRESS);
    abort();
}
