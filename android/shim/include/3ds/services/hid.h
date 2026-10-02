/**
 * @file hid.h
 * @brief libctru's HID (zlib licence, devkitPro), fed by the Android
 * controls (CtrHost_GetInput).
 *
 * hidScanInput takes one snapshot of the host input; held/down/up come from
 * consecutive snapshots exactly as in libctru. The KEY_CPAD_* bits are
 * derived from the circle pad the way the HID system module does.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    KEY_A = BIT(0),
    KEY_B = BIT(1),
    KEY_SELECT = BIT(2),
    KEY_START = BIT(3),
    KEY_DRIGHT = BIT(4),
    KEY_DLEFT = BIT(5),
    KEY_DUP = BIT(6),
    KEY_DDOWN = BIT(7),
    KEY_R = BIT(8),
    KEY_L = BIT(9),
    KEY_X = BIT(10),
    KEY_Y = BIT(11),
    KEY_ZL = BIT(14),
    KEY_ZR = BIT(15),
    KEY_TOUCH = BIT(20),
    KEY_CSTICK_RIGHT = BIT(24),
    KEY_CSTICK_LEFT = BIT(25),
    KEY_CSTICK_UP = BIT(26),
    KEY_CSTICK_DOWN = BIT(27),
    KEY_CPAD_RIGHT = BIT(28),
    KEY_CPAD_LEFT = BIT(29),
    KEY_CPAD_UP = BIT(30),
    KEY_CPAD_DOWN = BIT(31),

    KEY_UP = KEY_DUP | KEY_CPAD_UP,
    KEY_DOWN = KEY_DDOWN | KEY_CPAD_DOWN,
    KEY_LEFT = KEY_DLEFT | KEY_CPAD_LEFT,
    KEY_RIGHT = KEY_DRIGHT | KEY_CPAD_RIGHT,
};

typedef struct
{
    u16 px;
    u16 py;
} touchPosition;

typedef struct
{
    s16 dx;
    s16 dy;
} circlePosition;

Result hidInit(void);
void hidExit(void);
void hidSetRepeatParameters(u32 delay, u32 interval);
void hidScanInput(void);
u32 hidKeysHeld(void);
u32 hidKeysDown(void);
u32 hidKeysDownRepeat(void);
u32 hidKeysUp(void);
void hidTouchRead(touchPosition *pos);
void hidCircleRead(circlePosition *pos);

#ifdef __cplusplus
}
#endif
