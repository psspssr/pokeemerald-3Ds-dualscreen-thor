/**
 * @file mappable.h
 * @brief libctru's mappable address allocator (zlib licence, devkitPro).
 *
 * Only what __system_allocateHeaps calls. There is nothing to map memory
 * blocks into on Android, so the range is only recorded.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void mappableInit(u32 addrMin, u32 addrMax);

#ifdef __cplusplus
}
#endif
