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
Result GX_DisplayTransfer(u32*,u32,u32*,u32,u32);
Result GX_TextureCopy(u32*,u32,u32*,u32,u32,u32);
Result GX_MemoryFill(u32*,u32,u32*,u16,u32*,u32,u32*,u16);
