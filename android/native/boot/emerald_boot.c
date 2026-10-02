/*
 * libemeraldboot.so: loads libemerald.so at its link address.
 *
 * The game library must run where it was linked (see
 * android/native/src/load_address.c), so this reserves exactly that range and
 * hands it to the dynamic linker with ANDROID_DLEXT_RESERVED_ADDRESS, which
 * then maps the library with a load bias of zero. The app loads it first:
 *
 *     System.loadLibrary("emeraldboot");
 *     System.loadLibrary("emerald");
 *
 * The second call finds the library already loaded in the same namespace
 * (by soname) and only runs its JNI_OnLoad.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include <android/dlext.h>
#include <android/log.h>
#include <jni.h>

#include "boot_span.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define TAG "emeraldboot"

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    void *want = (void *)(uintptr_t)CTR_LOAD_ADDRESS;
    void *region, *handle;
    android_dlextinfo info;

    (void)vm;
    (void)reserved;
    if (dlopen("libemerald.so", RTLD_NOW | RTLD_NOLOAD) != NULL)
        return JNI_VERSION_1_6;

    /* Kernels before 4.17 ignore MAP_FIXED_NOREPLACE and treat the address as
     * a hint, so the result is checked rather than trusted. */
    region = mmap(want, CTR_LOAD_SPAN, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (region == MAP_FAILED || region != want)
    {
        __android_log_print(ANDROID_LOG_FATAL, TAG,
                            "cannot reserve %#x bytes at %p for libemerald.so (got %p, %s)",
                            (unsigned)CTR_LOAD_SPAN, want, region == MAP_FAILED ? NULL : region,
                            region == MAP_FAILED ? strerror(errno) : "address in use");
        if (region != MAP_FAILED)
            munmap(region, CTR_LOAD_SPAN);
        return JNI_ERR;
    }

    memset(&info, 0, sizeof(info));
    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = region;
    info.reserved_size = CTR_LOAD_SPAN;
    handle = android_dlopen_ext("libemerald.so", RTLD_NOW, &info);
    if (handle == NULL)
    {
        __android_log_print(ANDROID_LOG_FATAL, TAG, "cannot load libemerald.so: %s", dlerror());
        munmap(region, CTR_LOAD_SPAN);
        return JNI_ERR;
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "libemerald.so loaded at %p (%#x bytes)",
                        region, (unsigned)CTR_LOAD_SPAN);
    return JNI_VERSION_1_6;
}
