/**
 * @file romfs.h
 * @brief libctru's RomFS mount (zlib licence, devkitPro).
 *
 * The RomFS is a directory the app extracted (CtrHost_RomfsDir()); mounting
 * checks that it exists and makes "romfs:" paths resolve into it (see
 * ctrshim.h). Only the default "romfs" mount name is supported.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

Result romfsMountSelf(const char *name);
Result romfsUnmount(const char *name);

static inline Result romfsInit(void)
{
    return romfsMountSelf("romfs");
}

static inline Result romfsExit(void)
{
    return romfsUnmount("romfs");
}

#ifdef __cplusplus
}
#endif
