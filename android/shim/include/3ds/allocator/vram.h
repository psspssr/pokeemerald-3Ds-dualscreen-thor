/**
 * @file vram.h
 * @brief libctru's VRAM allocator (zlib licence, devkitPro), emulated.
 *
 * Blocks are ordinary zeroed CPU memory, recorded in ctrshim_mem.h. Space is
 * accounted like libctru, with Android's two 8 MiB banks (A and B): first fit
 * within a bank, VRAM_ALLOC_ANY trying the emptier bank first. A block never
 * spans the banks, so the largest possible allocation is 8 MiB. The 16 MiB
 * quota is backed on demand; real heap allocation failure still returns NULL.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vramAllocPos
{
    VRAM_ALLOC_A = BIT(0),
    VRAM_ALLOC_B = BIT(1),
    VRAM_ALLOC_ANY = VRAM_ALLOC_A | VRAM_ALLOC_B,
} vramAllocPos;

void *vramMemAlignAt(size_t size, size_t alignment, vramAllocPos pos);
void *vramMemAlign(size_t size, size_t alignment);
void *vramAllocAt(size_t size, vramAllocPos pos);
/* 0x80-aligned. */
void *vramAlloc(size_t size);
void *vramRealloc(void *mem, size_t size);
size_t vramGetSize(void *mem);
void vramFree(void *mem);
u32 vramSpaceFree(void);

#ifdef __cplusplus
}
#endif
