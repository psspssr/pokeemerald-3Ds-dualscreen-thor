/*
 * TEST STAND-IN for android/gpu/include/3ds/gfx.h (owned by the GPU agent).
 * Only on the include path of android/shim/test, after the real directory,
 * so the real header wins once it exists.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/services/gspgpu.h>

typedef enum
{
    GFX_TOP = 0,
    GFX_BOTTOM = 1,
} gfxScreen_t;

typedef enum
{
    GFX_LEFT = 0,
    GFX_RIGHT = 1,
} gfx3dSide_t;

void gfxInitDefault(void);
void gfxExit(void);
void gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat format);
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen);
void gfxSetDoubleBuffering(gfxScreen_t screen, bool enable);
u8 *gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16 *width, u16 *height);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gfxSwapBuffersGpu(void);
bool gfxIsWide(void);
