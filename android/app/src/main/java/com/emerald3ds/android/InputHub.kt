package com.emerald3ds.android

import kotlin.math.abs

/**
 * Merges every input source into the one CtrHostInput the game reads. UI
 * thread only. Keys are OR-ed; the on-screen circle pad wins over a stick
 * while it is held; the first source touching the bottom screen owns the
 * touch.
 */
object InputHub {
    const val SRC_OVERLAY = 0
    const val SRC_PRESENTATION = 1
    const val SRC_GAMEPAD = 2
    const val SRC_KEYBOARD = 3
    const val SRC_HAT = 4
    const val SRC_AXES = 5
    private const val SOURCES = 6

    private val keys = IntArray(SOURCES)
    private val circleX = IntArray(SOURCES)
    private val circleY = IntArray(SOURCES)
    private val circleActive = BooleanArray(SOURCES)
    private val touching = BooleanArray(SOURCES)
    private val touchX = IntArray(SOURCES)
    private val touchY = IntArray(SOURCES)
    private var touchOwner = -1

    private var lastKeys = 0
    private var lastCircleX = 0
    private var lastCircleY = 0
    private var lastTouchX = 0
    private var lastTouchY = 0

    /** What was last sent to the game, for tests. */
    var sentKeys = 0
        private set
    var sentTouchX = 0
        private set
    var sentTouchY = 0
        private set

    fun setKeys(source: Int, value: Int) {
        if (keys[source] == value) return
        keys[source] = value
        push()
    }

    fun setCircle(source: Int, x: Int, y: Int, active: Boolean = x != 0 || y != 0) {
        if (circleX[source] == x && circleY[source] == y && circleActive[source] == active) return
        circleX[source] = x
        circleY[source] = y
        circleActive[source] = active
        push()
    }

    /** x, y in bottom-screen pixels. Returns false if another source owns the touch. */
    fun setTouch(source: Int, active: Boolean, x: Int, y: Int): Boolean {
        if (active && touchOwner != -1 && touchOwner != source) return false
        touching[source] = active
        touchX[source] = x.coerceIn(0, CtrKeys.BOTTOM_WIDTH - 1)
        touchY[source] = y.coerceIn(0, CtrKeys.BOTTOM_HEIGHT - 1)
        touchOwner = if (active) source else if (touchOwner == source) -1 else touchOwner
        push()
        return true
    }

    fun clear() {
        keys.fill(0)
        circleX.fill(0)
        circleY.fill(0)
        circleActive.fill(false)
        touching.fill(false)
        touchOwner = -1
        push()
    }

    private fun push() {
        var k = 0
        for (v in keys) k = k or v
        var cx = circleX[SRC_OVERLAY]
        var cy = circleY[SRC_OVERLAY]
        if (!circleActive[SRC_OVERLAY]) {
            cx = circleX[SRC_AXES]
            cy = circleY[SRC_AXES]
        }
        if (cx > CtrKeys.CIRCLE_KEY_THRESHOLD) k = k or CtrKeys.CPAD_RIGHT
        if (cx < -CtrKeys.CIRCLE_KEY_THRESHOLD) k = k or CtrKeys.CPAD_LEFT
        if (cy > CtrKeys.CIRCLE_KEY_THRESHOLD) k = k or CtrKeys.CPAD_UP
        if (cy < -CtrKeys.CIRCLE_KEY_THRESHOLD) k = k or CtrKeys.CPAD_DOWN
        var tx = lastTouchX
        var ty = lastTouchY
        if (touchOwner >= 0 && touching[touchOwner]) {
            k = k or CtrKeys.TOUCH
            tx = touchX[touchOwner]
            ty = touchY[touchOwner]
        }
        if (k == lastKeys && cx == lastCircleX && cy == lastCircleY && tx == lastTouchX && ty == lastTouchY) return
        lastKeys = k
        lastCircleX = cx
        lastCircleY = cy
        lastTouchX = tx
        lastTouchY = ty
        sentKeys = k
        sentTouchX = tx
        sentTouchY = ty
        NativeBridge.setInput(k, cx, cy, tx, ty)
    }

    fun axisToCircle(value: Float, deadZone: Float): Int {
        if (abs(value) < deadZone) return 0
        val sign = if (value < 0) -1 else 1
        val scaled = (abs(value) - deadZone) / (1f - deadZone)
        return sign * (scaled.coerceAtMost(1f) * CtrKeys.CIRCLE_MAX).toInt()
    }
}
