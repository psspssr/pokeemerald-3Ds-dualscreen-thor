/**
 * @file dsp.h
 * @brief The part of libctru's DSP service (zlib licence, devkitPro) NDSP
 * users call. There is no DSP cache to manage on Android.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

Result DSP_FlushDataCache(const void *address, u32 size);
Result DSP_InvalidateDataCache(const void *address, u32 size);

#ifdef __cplusplus
}
#endif
