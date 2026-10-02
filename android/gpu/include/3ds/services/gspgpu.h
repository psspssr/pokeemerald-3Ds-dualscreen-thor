#pragma once
#include <3ds/types.h>
typedef enum {
    GSP_RGBA8_OES, GSP_BGR8_OES, GSP_RGB565_OES, GSP_RGB5_A1_OES, GSP_RGBA4_OES
} GSPGPU_FramebufferFormat;
Result GSPGPU_FlushDataCache(const void *address, u32 size);
Result GSPGPU_InvalidateDataCache(const void *address, u32 size);
void gspWaitForVBlank(void);
#define gspWaitForVBlank0() gspWaitForVBlank()
#define gspWaitForVBlank1() gspWaitForVBlank()
