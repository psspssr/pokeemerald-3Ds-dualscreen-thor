package com.emerald3ds.android

/** libctru's KEY_* bits (3ds/services/hid.h). */
object CtrKeys {
    const val A = 1 shl 0
    const val B = 1 shl 1
    const val SELECT = 1 shl 2
    const val START = 1 shl 3
    const val DRIGHT = 1 shl 4
    const val DLEFT = 1 shl 5
    const val DUP = 1 shl 6
    const val DDOWN = 1 shl 7
    const val R = 1 shl 8
    const val L = 1 shl 9
    const val X = 1 shl 10
    const val Y = 1 shl 11
    const val ZL = 1 shl 14
    const val ZR = 1 shl 15
    const val TOUCH = 1 shl 20
    const val CSTICK_RIGHT = 1 shl 24
    const val CSTICK_LEFT = 1 shl 25
    const val CSTICK_UP = 1 shl 26
    const val CSTICK_DOWN = 1 shl 27
    const val CPAD_RIGHT = 1 shl 28
    const val CPAD_LEFT = 1 shl 29
    const val CPAD_UP = 1 shl 30
    const val CPAD_DOWN = 1 shl 31

    const val DPAD = DRIGHT or DLEFT or DUP or DDOWN

    /** libctru's circle pad range. */
    const val CIRCLE_MAX = 156
    /** Beyond this the HID module reports the KEY_CPAD_* bits. */
    const val CIRCLE_KEY_THRESHOLD = 40

    const val TOP_WIDTH = 400
    const val TOP_HEIGHT = 240
    const val BOTTOM_WIDTH = 320
    const val BOTTOM_HEIGHT = 240
}
