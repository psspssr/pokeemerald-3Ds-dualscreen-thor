/* TEST STAND-IN for android/gpu/include/3ds/services/gspgpu.h (GPU agent). */
#pragma once

#include <3ds/types.h>

typedef enum
{
    GSP_RGBA8_OES = 0,
    GSP_BGR8_OES = 1,
    GSP_RGB565_OES = 2,
    GSP_RGB5_A1_OES = 3,
    GSP_RGBA4_OES = 4,
} GSPGPU_FramebufferFormat;

Result GSPGPU_FlushDataCache(const void *adr, u32 size);
void gspWaitForVBlank(void);
