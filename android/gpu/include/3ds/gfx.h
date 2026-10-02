#pragma once
#include <3ds/types.h>
#include <3ds/services/gspgpu.h>
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;
void gfxInitDefault(void);
void gfxInit(GSPGPU_FramebufferFormat top, GSPGPU_FramebufferFormat bottom, bool vram);
void gfxExit(void);
void gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat format);
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen);
void gfxSetDoubleBuffering(gfxScreen_t screen, bool enable);
u8 *gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16 *width, u16 *height);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gfxSwapBuffersGpu(void);
void gfxSet3D(bool enable);
void gfxSetWide(bool enable);
bool gfxIs3D(void);
bool gfxIsWide(void);
