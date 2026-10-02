/*
 * Native half of the bottom screen: the only part that touches libctru.
 *
 * CtrLog_Init leaves the bottom screen as a single RGB565 buffer that nothing
 * else draws to (the diagnostic console only takes it over on a fatal error),
 * and Citro3D never renders to it. The game-side canvas already has the
 * framebuffer's layout, so presenting is a copy of whole columns and a data
 * cache flush over the same range: the LCD reads the buffer from memory.
 */
#include <3ds.h>
#include <string.h>
#include "3ds_bottom.h"

static u16 *Framebuffer(void)
{
    if (gfxGetScreenFormat(GFX_BOTTOM) != GSP_RGB565_OES)
        return NULL;
    return (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
}

void CtrBottom_Blit(const uint16_t *canvas, int x0, int x1)
{
    u16 *fb = Framebuffer();

    if (!fb || x0 >= x1) return;
    memcpy(fb + x0 * CTR_BOTTOM_HEIGHT, canvas + x0 * CTR_BOTTOM_HEIGHT,
           (size_t)(x1 - x0) * CTR_BOTTOM_HEIGHT * sizeof(u16));
    GSPGPU_FlushDataCache(fb + x0 * CTR_BOTTOM_HEIGHT, (u32)(x1 - x0) * CTR_BOTTOM_HEIGHT * sizeof(u16));
}

void CtrBottom_BlitRect(const uint16_t *canvas, int x0, int y0, int x1, int y1)
{
    u16 *fb = Framebuffer();
    /* A column runs bottom-to-top, so rows [y0, y1) are one contiguous run. */
    int first = CTR_BOTTOM_HEIGHT - y1, count = y1 - y0;

    if (!fb || x0 >= x1 || count <= 0) return;
    for (int x = x0; x < x1; ++x)
        memcpy(fb + x * CTR_BOTTOM_HEIGHT + first, canvas + x * CTR_BOTTOM_HEIGHT + first,
               (size_t)count * sizeof(u16));
    GSPGPU_FlushDataCache(fb + x0 * CTR_BOTTOM_HEIGHT, (u32)(x1 - x0) * CTR_BOTTOM_HEIGHT * sizeof(u16));
}
