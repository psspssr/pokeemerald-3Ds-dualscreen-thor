package com.emerald3ds.android

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.os.SystemClock
import kotlin.math.abs
import kotlin.math.max

/**
 * Gamepads and keyboards. Face buttons map by position by default, like a
 * Nintendo console: Android's BUTTON_A is the bottom face button, which is
 * the 3DS's B.
 */
class PhysicalInput(private val callbacks: Callbacks) {
    interface Callbacks {
        fun onPhysicalInput()
        fun onMenuKey()
        fun onToggleBottomScreen()
        fun onFastForwardToggle() {}
        fun onFastForwardHold(held: Boolean) {}
    }

    var labelMapping = false
    var fastForwardEnabled = false
        set(value) {
            if (field != value) clearFastTriggers()
            field = value
        }
    private val fastKeys = mutableSetOf<Pair<Int, Int>>()
    private val fastAxes = mutableMapOf<Int, Pair<Boolean, Boolean>>()
    private var fastToggleDown = false
    private var fastHoldDown = false
    private var fastToggleNeedsRelease = false
    private var fastSuspendedAt = Long.MIN_VALUE

    // Track physical keys individually: left/right Shift and multiple
    // controllers may hold the same logical 3DS button at the same time.
    private val gamepadKeys = mutableMapOf<Pair<Int, Int>, Int>()
    private val keyboardKeys = mutableMapOf<Pair<Int, Int>, Int>()
    private var motionDevice: Int? = null

    private fun gamepadKey(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_A -> if (labelMapping) CtrKeys.A else CtrKeys.B
        KeyEvent.KEYCODE_BUTTON_B -> if (labelMapping) CtrKeys.B else CtrKeys.A
        KeyEvent.KEYCODE_BUTTON_X -> if (labelMapping) CtrKeys.X else CtrKeys.Y
        KeyEvent.KEYCODE_BUTTON_Y -> if (labelMapping) CtrKeys.Y else CtrKeys.X
        KeyEvent.KEYCODE_BUTTON_L1 -> CtrKeys.L
        KeyEvent.KEYCODE_BUTTON_R1 -> CtrKeys.R
        KeyEvent.KEYCODE_BUTTON_L2 -> CtrKeys.ZL
        KeyEvent.KEYCODE_BUTTON_R2 -> CtrKeys.ZR
        KeyEvent.KEYCODE_BUTTON_START -> CtrKeys.START
        KeyEvent.KEYCODE_BUTTON_SELECT -> CtrKeys.SELECT
        KeyEvent.KEYCODE_DPAD_UP -> CtrKeys.DUP
        KeyEvent.KEYCODE_DPAD_DOWN -> CtrKeys.DDOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> CtrKeys.DLEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> CtrKeys.DRIGHT
        KeyEvent.KEYCODE_DPAD_UP_LEFT -> CtrKeys.DUP or CtrKeys.DLEFT
        KeyEvent.KEYCODE_DPAD_UP_RIGHT -> CtrKeys.DUP or CtrKeys.DRIGHT
        KeyEvent.KEYCODE_DPAD_DOWN_LEFT -> CtrKeys.DDOWN or CtrKeys.DLEFT
        KeyEvent.KEYCODE_DPAD_DOWN_RIGHT -> CtrKeys.DDOWN or CtrKeys.DRIGHT
        KeyEvent.KEYCODE_DPAD_CENTER -> CtrKeys.A
        else -> 0
    }

    private fun keyboardKey(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_X -> CtrKeys.A
        KeyEvent.KEYCODE_Z -> CtrKeys.B
        KeyEvent.KEYCODE_S -> CtrKeys.X
        KeyEvent.KEYCODE_A -> CtrKeys.Y
        KeyEvent.KEYCODE_Q -> CtrKeys.L
        KeyEvent.KEYCODE_W -> CtrKeys.R
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> CtrKeys.START
        KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> CtrKeys.SELECT
        else -> 0
    }

    /** Returns true if the event was consumed. */
    fun onKey(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return false
        val code = event.keyCode
        val down = event.action == KeyEvent.ACTION_DOWN
        if (fastForwardEnabled && code in setOf(KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_TAB)) {
            if (down && event.repeatCount > 0) return true
            // A fresh digital press is an explicit edge, unlike replayed
            // joystick state after a pause/dialog. A release may have reached
            // the dialog while game input was suspended.
            if (down && code == KeyEvent.KEYCODE_BUTTON_R2 && fastToggleNeedsRelease && event.downTime > fastSuspendedAt) {
                fastToggleNeedsRelease = false
                fastToggleDown = false
            }
            val identity = event.deviceId to code
            if (down) fastKeys.add(identity) else fastKeys.remove(identity)
            updateFastTriggers()
            if (down) callbacks.onPhysicalInput()
            return true
        }
        when (code) {
            KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_MENU -> {
                if (down && event.repeatCount == 0) callbacks.onMenuKey()
                return true
            }
            KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR -> {
                if (down && event.repeatCount == 0) {
                    callbacks.onPhysicalInput()
                    callbacks.onToggleBottomScreen()
                }
                return true
            }
        }
        val pad = gamepadKey(code)
        val keyboard = if (pad == 0) keyboardKey(code) else 0
        if (pad == 0 && keyboard == 0) return false
        if (down && event.repeatCount > 0) return true
        val identity = event.deviceId to code
        if (pad != 0) {
            if (down) gamepadKeys[identity] = pad else gamepadKeys.remove(identity)
            InputHub.setKeys(InputHub.SRC_GAMEPAD, gamepadKeys.values.fold(0) { a, b -> a or b })
        } else {
            if (down) keyboardKeys[identity] = keyboard else keyboardKeys.remove(identity)
            InputHub.setKeys(InputHub.SRC_KEYBOARD, keyboardKeys.values.fold(0) { a, b -> a or b })
        }
        if (down) callbacks.onPhysicalInput()
        return true
    }

    fun onMotion(event: MotionEvent): Boolean {
        val joystick = event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.isFromSource(InputDevice.SOURCE_GAMEPAD)
        if (!joystick || event.action != MotionEvent.ACTION_MOVE) return false
        motionDevice = event.deviceId
        val device = event.device

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        var hat = 0
        if (hatX < -0.5f) hat = hat or CtrKeys.DLEFT
        if (hatX > 0.5f) hat = hat or CtrKeys.DRIGHT
        if (hatY < -0.5f) hat = hat or CtrKeys.DUP
        if (hatY > 0.5f) hat = hat or CtrKeys.DDOWN

        val lx = InputHub.axisToCircle(event.getAxisValue(MotionEvent.AXIS_X), deadZone(device, MotionEvent.AXIS_X, event))
        val ly = -InputHub.axisToCircle(event.getAxisValue(MotionEvent.AXIS_Y), deadZone(device, MotionEvent.AXIS_Y, event))

        var axes = 0
        val rx = event.getAxisValue(MotionEvent.AXIS_Z)
        val ry = event.getAxisValue(MotionEvent.AXIS_RZ)
        if (rx < -0.5f) axes = axes or CtrKeys.CSTICK_LEFT
        if (rx > 0.5f) axes = axes or CtrKeys.CSTICK_RIGHT
        if (ry < -0.5f) axes = axes or CtrKeys.CSTICK_UP
        if (ry > 0.5f) axes = axes or CtrKeys.CSTICK_DOWN
        val leftTrigger = max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE))
        val rightTrigger = max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS))
        val previousTriggers = fastAxes[event.deviceId] ?: (false to false)
        fun trigger(value: Float, wasDown: Boolean): Boolean =
            if (fastForwardEnabled) value >= 0.55f || (wasDown && value > 0.4f) else value > 0.5f
        val l2 = trigger(leftTrigger, previousTriggers.first)
        val r2 = trigger(rightTrigger, previousTriggers.second)
        if (fastForwardEnabled) {
            fastAxes[event.deviceId] = l2 to r2
            updateFastTriggers()
        } else {
            if (l2) axes = axes or CtrKeys.ZL
            if (r2) axes = axes or CtrKeys.ZR
        }

        InputHub.setKeys(InputHub.SRC_HAT, hat)
        InputHub.setKeys(InputHub.SRC_AXES, axes)
        InputHub.setCircle(InputHub.SRC_AXES, lx, ly)
        if (hat != 0 || axes != 0 || l2 || r2 || abs(lx) > 0 || abs(ly) > 0) callbacks.onPhysicalInput()
        return true
    }

    private fun deadZone(device: InputDevice?, axis: Int, event: MotionEvent): Float {
        val flat = device?.getMotionRange(axis, event.source)?.flat ?: 0f
        return max(flat, 0.12f)
    }

    fun clear() {
        clearFastTriggers()
        gamepadKeys.clear()
        keyboardKeys.clear()
        motionDevice = null
        InputHub.setKeys(InputHub.SRC_GAMEPAD, 0)
        InputHub.setKeys(InputHub.SRC_KEYBOARD, 0)
        clearMotion()
    }

    fun removeDevice(deviceId: Int) {
        fastKeys.removeAll { it.first == deviceId }
        fastAxes.remove(deviceId)
        updateFastTriggers()
        gamepadKeys.keys.removeAll { it.first == deviceId }
        keyboardKeys.keys.removeAll { it.first == deviceId }
        InputHub.setKeys(InputHub.SRC_GAMEPAD, gamepadKeys.values.fold(0) { a, b -> a or b })
        InputHub.setKeys(InputHub.SRC_KEYBOARD, keyboardKeys.values.fold(0) { a, b -> a or b })
        if (motionDevice == deviceId) {
            motionDevice = null
            clearMotion()
        }
    }

    private fun clearMotion() {
        InputHub.setKeys(InputHub.SRC_HAT, 0)
        InputHub.setKeys(InputHub.SRC_AXES, 0)
        InputHub.setCircle(InputHub.SRC_AXES, 0, 0)
    }

    private fun updateFastTriggers() {
        // Some controllers send both key and axis events for one trigger.
        // Merge them before detecting an edge, so R2 toggles exactly once.
        val toggle = fastKeys.any { it.second == KeyEvent.KEYCODE_BUTTON_R2 } || fastAxes.values.any { it.second }
        val hold = fastKeys.any { it.second != KeyEvent.KEYCODE_BUTTON_R2 } || fastAxes.values.any { it.first }
        if (!toggle) fastToggleNeedsRelease = false
        if (toggle && !fastToggleDown && !fastToggleNeedsRelease) callbacks.onFastForwardToggle()
        fastToggleDown = toggle
        if (hold != fastHoldDown) {
            fastHoldDown = hold
            callbacks.onFastForwardHold(hold)
        }
    }

    private fun clearFastTriggers() {
        fastToggleNeedsRelease = fastToggleNeedsRelease || fastToggleDown
        fastSuspendedAt = SystemClock.uptimeMillis()
        fastKeys.clear()
        fastAxes.clear()
        fastToggleDown = false
        fastHoldDown = false
        callbacks.onFastForwardHold(false)
    }
}
