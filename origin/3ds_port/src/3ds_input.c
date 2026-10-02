#include <3ds.h>
#include <string.h>
#include "3ds_platform.h"

static CtrInput sInput;

void CtrInput_Clear(void)
{
    memset(&sInput, 0, sizeof(sInput));
}

void CtrInput_Scan(void)
{
    static const uint32_t keys[] =
    {
        KEY_A, KEY_B, KEY_SELECT, KEY_START, KEY_DRIGHT,
        KEY_DLEFT, KEY_DUP, KEY_DDOWN, KEY_R, KEY_L, KEY_X, KEY_Y
    };
    CtrInputSample sample = {0};
    hidScanInput();
    uint32_t raw = hidKeysHeld();
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (raw & keys[i])
            sample.buttons |= 1u << i;
    circlePosition circle;
    hidCircleRead(&circle);
    sample.circleX = circle.dx;
    sample.circleY = circle.dy;
    sample.touchActive = (raw & KEY_TOUCH) != 0;
    if (sample.touchActive)
    {
        touchPosition touch;
        hidTouchRead(&touch);
        sample.touchX = touch.px;
        sample.touchY = touch.py;
    }
    CtrInput_Update(&sInput, &sample);
}

const CtrInput *CtrInput_Get(void)
{
    return &sInput;
}
