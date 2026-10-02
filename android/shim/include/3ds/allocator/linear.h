/**
 * @file linear.h
 * @brief libctru's linear heap (zlib licence, devkitPro), emulated.
 *
 * Blocks are ordinary zeroed CPU memory, recorded in ctrshim_mem.h. Space is
 * accounted with libctru's own first-fit pool over a virtual range of
 * __ctru_linear_heap_size bytes (32 MiB unless the application defines the
 * variable, as origin does), so allocation fails, fragments and reports free
 * space as it would on the console.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void *linearMemAlign(size_t size, size_t alignment);
/* 0x80-aligned. */
void *linearAlloc(size_t size);
/* Not implemented by libctru either: always NULL. */
void *linearRealloc(void *mem, size_t size);
size_t linearGetSize(void *mem);
void linearFree(void *mem);
u32 linearSpaceFree(void);

#ifdef __cplusplus
}
#endif
