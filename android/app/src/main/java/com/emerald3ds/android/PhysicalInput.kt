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
    private val fastToggleNeedsRelease = mutableSetOf<Int>()
    private var fastSuspendedAt = Long.MIN_VALUE

    // Track physical keys individually: left/right Shift and multiple
    // controllers may hold the same logical 3DS button at the same time.
    private val gamepadKeys = mutableMapOf<Pair<Int, Int>, Int>()
    private val keyboardKeys = mutableMapOf<Pair<Int, Int>, Int>()
    private var motionDevice: Int? = null
    private val shoulderPresses = mutableMapOf<Pair<Int, Int>, Long>()
    private val blockedShoulders = mutableMapOf<Pair<Int, Int>, Long>()
    private var shoulderInputAfter = Long.MIN_VALUE
    private data class KeyDownStamp(val time: Long, var code: Int)
    private val latestKeyDowns = mutableMapOf<Int, KeyDownStamp>()

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

    /** Observe releases even when a dialog owns the event. This is deliberately
     * non-consuming and does not send any buttons to the game. */
    fun observeKeyEvent(event: KeyEvent, suspended: Boolean = false) {
        if (fastForwardEnabled && event.keyCode == KeyEvent.KEYCODE_BUTTON_R2 &&
            ((suspended && event.action == KeyEvent.ACTION_DOWN) ||
                (event.action == KeyEvent.ACTION_UP && event.isCanceled))) fastToggleNeedsRelease.add(event.deviceId)
        // A dialog-owned R2 UP must not clear this guard: the controller's
        // analog trigger may still be held inside its hysteresis band.
        if (suspended && event.action == KeyEvent.ACTION_DOWN &&
            (event.keyCode == KeyEvent.KEYCODE_BUTTON_L1 || event.keyCode == KeyEvent.KEYCODE_BUTTON_R1)) {
            // Shoulders first pressed in a dialog are held at the next
            // resume too, even though no game button was ever sent.
            val identity = event.deviceId to event.keyCode
            blockedShoulders[identity] = max(blockedShoulders[identity] ?: Long.MIN_VALUE, event.eventTime)
        }
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0 && event.downTime == event.eventTime) {
            val previous = latestKeyDowns[event.deviceId]
            if (previous == null || event.eventTime > previous.time)
                latestKeyDowns[event.deviceId] = KeyDownStamp(event.eventTime, event.keyCode)
            else if (event.eventTime == previous.time && event.keyCode != previous.code)
                previous.code = KeyEvent.KEYCODE_UNKNOWN // More than one key used this timestamp.
        } else if (event.action == KeyEvent.ACTION_UP &&
            (event.keyCode == KeyEvent.KEYCODE_BUTTON_L1 || event.keyCode == KeyEvent.KEYCODE_BUTTON_R1)) {
            val identity = event.deviceId to event.keyCode
            shoulderPresses.remove(identity)
            if (event.isCanceled) {
                // Focus loss can synthesize a canceled UP while the physical
                // shoulder remains held. It cannot prove a release.
                blockedShoulders[identity] = max(blockedShoulders[identity] ?: Long.MIN_VALUE, event.eventTime)
            } else blockedShoulders.remove(identity)
        }
    }

    /** Returns true if the event was consumed. */
    fun onKey(event: KeyEvent): Boolean {
        observeKeyEvent(event)
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return false
        val code = event.keyCode
        val down = event.action == KeyEvent.ACTION_DOWN
        if (fastForwardEnabled && code in setOf(KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_TAB)) {
            if (down && event.repeatCount > 0) return true
            // Held hardware repeats can have count zero and a downTime
            // rebased by another key. Recover a missed release only from a
            // distinct new DOWN after resume, without a timestamp collision.
            val stamp = latestKeyDowns[event.deviceId]
            if (down && code == KeyEvent.KEYCODE_BUTTON_R2 && event.downTime == event.eventTime &&
                event.eventTime > fastSuspendedAt &&
                (stamp?.time != event.eventTime || stamp.code == code))
                fastToggleNeedsRelease.remove(event.deviceId)
            val identity = event.deviceId to code
            if (down) fastKeys.add(identity) else fastKeys.remove(identity)
            // A cleared axis map after pause means unknown, not neutral.
            // Digital-only pads can recover via the checked fresh DOWN above.
            if (!down && code == KeyEvent.KEYCODE_BUTTON_R2 && !event.isCanceled && fastAxes[event.deviceId]?.second == false)
                fastToggleNeedsRelease.remove(event.deviceId)
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
            if (code == KeyEvent.KEYCODE_BUTTON_L1 || code == KeyEvent.KEYCODE_BUTTON_R1) {
                if (down) {
                    // Android may rebase a held key's downTime when another
                    // button is pressed, even with repeatCount == 0. Only a
                    // DOWN at its own event time can recover a missed release.
                    val blockedAt = blockedShoulders[identity]
                    val stamp = latestKeyDowns[event.deviceId]
                    if (event.downTime != event.eventTime || event.eventTime < shoulderInputAfter) return true
                    // Identical millisecond timestamps are ambiguous for a
                    // blocked key: keep it blocked if another key used that
                    // timestamp, or it coincides with resuming input. Observed
                    // UPs remove the guard; normal simultaneous L+R still works.
                    if (blockedAt != null && (event.eventTime <= blockedAt ||
                        event.eventTime <= shoulderInputAfter ||
                        (stamp?.time == event.eventTime && stamp.code != code))) return true
                    blockedShoulders.remove(identity)
                    shoulderPresses[identity] = event.eventTime
                    val other = event.deviceId to if (code == KeyEvent.KEYCODE_BUTTON_L1)
                        KeyEvent.KEYCODE_BUTTON_R1 else KeyEvent.KEYCODE_BUTTON_L1
                    if (other in shoulderPresses && other !in blockedShoulders) {
                        blockedShoulders[identity] = event.eventTime
                        blockedShoulders[other] = shoulderPresses.getValue(other)
                        gamepadKeys.remove(identity)
                        gamepadKeys.remove(other)
                        InputHub.setKeys(InputHub.SRC_GAMEPAD, gamepadKeys.values.fold(0) { a, b -> a or b })
                        // The callback synchronously calls clear(). Latch
                        // first and return without restoring either button.
                        callbacks.onMenuKey()
                        return true
                    }
                }
            }
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
        val r2 = trigger(rightTrigger, previousTriggers.second || event.deviceId in fastToggleNeedsRelease)
        if (fastForwardEnabled) {
            fastAxes[event.deviceId] = l2 to r2
            if (!r2 && (event.deviceId to KeyEvent.KEYCODE_BUTTON_R2) !in fastKeys)
                fastToggleNeedsRelease.remove(event.deviceId)
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
        onInputResumed()
        clearFastTriggers()
        gamepadKeys.clear()
        keyboardKeys.clear()
        motionDevice = null
        InputHub.setKeys(InputHub.SRC_GAMEPAD, 0)
        InputHub.setKeys(InputHub.SRC_KEYBOARD, 0)
        clearMotion()
    }

    /** A resume blocks shoulders still held at the pause boundary. A release,
     * or an unambiguous new DOWN after a missed release, rearms each shoulder. */
    fun onInputResumed() {
        shoulderPresses.forEach { (identity, time) ->
            blockedShoulders[identity] = max(blockedShoulders[identity] ?: Long.MIN_VALUE, time)
        }
        shoulderInputAfter = SystemClock.uptimeMillis()
        fastSuspendedAt = max(fastSuspendedAt, shoulderInputAfter)
    }

    fun removeDevice(deviceId: Int) {
        shoulderPresses.keys.removeAll { it.first == deviceId }
        blockedShoulders.keys.removeAll { it.first == deviceId }
        latestKeyDowns.remove(deviceId)
        fastKeys.removeAll { it.first == deviceId }
        fastAxes.remove(deviceId)
        fastToggleNeedsRelease.remove(deviceId)
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
        val toggle = fastKeys.any { it.second == KeyEvent.KEYCODE_BUTTON_R2 && it.first !in fastToggleNeedsRelease } ||
            fastAxes.any { (device, axes) -> axes.second && device !in fastToggleNeedsRelease }
        val hold = fastKeys.any { it.second != KeyEvent.KEYCODE_BUTTON_R2 } || fastAxes.values.any { it.first }
        if (toggle && !fastToggleDown) callbacks.onFastForwardToggle()
        fastToggleDown = toggle
        if (hold != fastHoldDown) {
            fastHoldDown = hold
            callbacks.onFastForwardHold(hold)
        }
    }

    private fun clearFastTriggers() {
        // Keep the identity of each held trigger through a pause. A neutral
        // sample from another controller cannot release it, and its first
        // resumed axis sample must still use the pressed hysteresis threshold.
        fastKeys.filter { it.second == KeyEvent.KEYCODE_BUTTON_R2 }.forEach { fastToggleNeedsRelease.add(it.first) }
        fastAxes.filterValues { it.second }.keys.forEach(fastToggleNeedsRelease::add)
        fastSuspendedAt = SystemClock.uptimeMillis()
        fastKeys.clear()
        fastAxes.clear()
        fastToggleDown = false
        fastHoldDown = false
        callbacks.onFastForwardHold(false)
    }
}
