#ifndef CTRSHIM_MEM_H
#define CTRSHIM_MEM_H

/*
 * Emulated 3DS memory pools. linearAlloc/vramAlloc (android/shim) hand out
 * ordinary CPU memory and record every block here, so the GPU emulation
 * (android/gpu) can classify a pointer it is given: a transfer's source or
 * destination, a texture's data, a vertex buffer.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CTR_MEM_NONE,
    CTR_MEM_LINEAR,
    CTR_MEM_VRAM,
    /* An LCD framebuffer (android/gpu registers them). */
    CTR_MEM_FRAMEBUFFER,
} CtrMemKind;

typedef struct
{
    CtrMemKind kind;
    uint8_t *base;
    size_t size;
    /* Owner-defined: android/gpu attaches its texture/target/screen record. */
    void *owner;
} CtrMemBlock;

/* Emulated pool sizes reported by linearSpaceFree/vramSpaceFree. */
#define CTR_MEM_LINEAR_POOL (8u * 1024u * 1024u)
#define CTR_MEM_VRAM_POOL (6u * 1024u * 1024u)

/*
 * Blocks never overlap. Registering a range that starts at an existing
 * block's base relabels that block (a framebuffer carved out of a linearAlloc
 * block, say): kind, size and owner are replaced. Unregistering a relabelled
 * pool block gives it back its pool kind and size; unregistering any other
 * block removes it. linearFree/vramFree remove their block whatever its
 * label. A range overlapping another block in any other way is refused and
 * logged.
 */
void CtrMem_Register(CtrMemKind kind, void *base, size_t size, void *owner);
void CtrMem_Unregister(void *base);
/* The block containing ptr, or false. The copy is a snapshot. */
bool CtrMem_Find(const void *ptr, CtrMemBlock *out);
void CtrMem_SetOwner(void *base, void *owner);
size_t CtrMem_Used(CtrMemKind kind);

#ifdef __cplusplus
}
#endif

#endif
