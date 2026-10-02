/*
 * HID: one snapshot of the host's controls per hidScanInput, with libctru's
 * held/down/up/repeat bookkeeping.
 */
#include <3ds/services/hid.h>

#include "ctr_host.h"
#include "shim_internal.h"

#define CPAD_BITS (KEY_CPAD_RIGHT | KEY_CPAD_LEFT | KEY_CPAD_UP | KEY_CPAD_DOWN)

static u32 kOld, kHeld, kDown, kUp, kRepeat;
static u32 kDelay, kInterval, kCount;
static touchPosition tPos;
static circlePosition cPos;

/*
 * The HID module's circle pad directions: past a radius of 40, right/left
 * within 60 degrees of the horizontal axis and up/down within 60 degrees of
 * the vertical one, so diagonals set both.
 */
u32 CtrHid_CirclePadKeys(int x, int y)
{
    const float tan30 = 0.577350269f, tan60 = 1.0f / 0.577350269f;
    u32 keys = 0;
    float t;

    if (x * x + y * y <= 40 * 40)
        return 0;
    t = x != 0 ? (float)y / (float)x : 0.0f;
    if (t < 0.0f)
        t = -t;
    if (x != 0 && t < tan60)
        keys |= x > 0 ? KEY_CPAD_RIGHT : KEY_CPAD_LEFT;
    if (x == 0 || t > tan30)
        keys |= y > 0 ? KEY_CPAD_UP : KEY_CPAD_DOWN;
    return keys;
}

Result hidInit(void)
{
    return 0;
}

void hidExit(void)
{
}

void hidSetRepeatParameters(u32 delay, u32 interval)
{
    kDelay = delay;
    kInterval = interval;
    kCount = delay;
    kRepeat = 0;
}

void hidScanInput(void)
{
    CtrHostInput input;

    CtrHost_GetInput(&input);
    kOld = kHeld;
    cPos.dx = input.circleX;
    cPos.dy = input.circleY;
    kHeld = (input.keys & ~CPAD_BITS) | CtrHid_CirclePadKeys(input.circleX, input.circleY);
    if (kHeld & KEY_TOUCH)
    {
        tPos.px = input.touchX;
        tPos.py = input.touchY;
    }
    else
    {
        tPos.px = tPos.py = 0;
    }
    kDown = ~kOld & kHeld;
    kUp = kOld & ~kHeld;
    if (kDelay)
    {
        if (kHeld != kOld)
        {
            kCount = kDelay;
            kRepeat = kDown;
        }
        if (--kCount == 0)
        {
            kCount = kInterval;
            kRepeat = kHeld;
        }
    }
}

u32 hidKeysHeld(void)
{
    return kHeld;
}

u32 hidKeysDown(void)
{
    return kDown;
}

u32 hidKeysDownRepeat(void)
{
    u32 repeat = kRepeat;

    kRepeat = 0;
    return repeat;
}

u32 hidKeysUp(void)
{
    return kUp;
}

void hidTouchRead(touchPosition *pos)
{
    if (pos)
        *pos = tPos;
}

void hidCircleRead(circlePosition *pos)
{
    if (pos)
        *pos = cPos;
}
