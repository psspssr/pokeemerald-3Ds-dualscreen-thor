#pragma once
#include <3ds/types.h>
#define GX_BUFFER_DIM(w,h) ((u32)(w) | ((u32)(h)<<16))
#define GX_TRANSFER_FLIP_VERT(v) ((v)&1)
#define GX_TRANSFER_OUT_TILED(v) (((v)&1)<<1)
#define GX_TRANSFER_RAW_COPY(v) (((v)&1)<<3)
#define GX_TRANSFER_IN_FORMAT(v) (((v)&7)<<8)
#define GX_TRANSFER_OUT_FORMAT(v) (((v)&7)<<12)
#define GX_TRANSFER_SCALING(v) (((v)&3)<<24)
typedef enum { GX_TRANSFER_FMT_RGBA8, GX_TRANSFER_FMT_RGB8, GX_TRANSFER_FMT_RGB565,
    GX_TRANSFER_FMT_RGB5A1, GX_TRANSFER_FMT_RGBA4 } GX_TRANSFER_FORMAT;
typedef enum { GX_TRANSFER_SCALE_NO, GX_TRANSFER_SCALE_X, GX_TRANSFER_SCALE_XY } GX_TRANSFER_SCALE;
/* libctru's public queue metadata (devkitPro, zlib). GLES submits commands
 * directly; the counts retain Citro3D's per-frame upload budget contract. */
typedef union {
    u32 data[8];
    struct { u8 type, unk1, unk2, unk3; u32 args[7]; };
} gxCmdEntry_s;
typedef struct tag_gxCmdQueue_s {
    gxCmdEntry_s *entries;
    u16 maxEntries, numEntries, curEntry, lastEntry;
    void (*callback)(struct tag_gxCmdQueue_s *);
    void *user;
} gxCmdQueue_s;
void GX_BindQueue(gxCmdQueue_s *queue);
Result GX_DisplayTransfer(u32*,u32,u32*,u32,u32);
Result GX_TextureCopy(u32*,u32,u32*,u32,u32,u32);
Result GX_MemoryFill(u32*,u32,u32*,u16,u32*,u32,u32*,u16);
