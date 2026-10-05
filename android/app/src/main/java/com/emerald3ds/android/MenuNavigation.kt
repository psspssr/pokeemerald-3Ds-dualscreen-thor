package com.emerald3ds.android

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.Window
import androidx.preference.PreferenceManager
import java.lang.ref.WeakReference
import kotlin.math.abs

/** Controller navigation for our own UI windows. Android still owns focus,
 * list scrolling, text editing and SeekBar behavior. Never installed on SAF. */
internal object MenuNavigation {
    private const val GAME = 0L
    private const val CONFIRM = -1
    private const val BACK = -2
    private const val TEXT = -3
    private const val PRESS = 0.6f
    private const val RELEASE = 0.35f
    private const val REPEAT_DELAY = 360L
    private const val REPEAT_INTERVAL = 120L
    private val main = Handler(Looper.getMainLooper())
    private var nextOwner = 0L
    private var nextOrder = 0L
    private var active = WeakReference<NavigationCallback>(null)
    private data class Press(val owner: Long, val action: Int, val order: Long, var blocked: Boolean = false)
    private val keys = mutableMapOf<Pair<Int, Int>, Press>()
    private val axes = mutableMapOf<Int, Press>()

    var usingController = false
        private set

    /** ensureFocus returns true only when it establishes or restores focus. */
    fun install(window: Window, ensureFocus: (() -> Boolean)? = null) {
        val previous = window.callback
        if (previous is NavigationCallback) {
            if (ensureFocus != null) previous.ensureFocus = ensureFocus
        } else {
            window.callback = NavigationCallback(window, previous, ensureFocus ?: { false })
        }
        refreshFocus(window)
    }

    fun refreshFocus(window: Window, force: Boolean = false) {
        val navigation = window.callback as? NavigationCallback ?: return
        navigation.forceFocus = navigation.forceFocus || force
        window.decorView.post { navigation.prepareIfFocused() }
    }

    /** InputDispatcher can finish an old window's key stream after its dialog
     * dismissed and the Activity paused. Release evidence still belongs to us. */
    fun observeInactiveKey(event: KeyEvent) {
        if (!isNavigationKey(event.keyCode)) return
        val id = event.deviceId to event.keyCode
        if (event.action == KeyEvent.ACTION_UP && !event.isCanceled) keys.remove(id)
        else if (event.action == KeyEvent.ACTION_DOWN || event.action == KeyEvent.ACTION_UP)
            keys.getOrPut(id) { Press(GAME, 0, ++nextOrder) }.blocked = true
        active.get()?.updateDirection()
    }

    fun observeInactiveMotion(event: MotionEvent) {
        if (!isJoystick(event)) return
        if (event.actionMasked == MotionEvent.ACTION_MOVE) {
            if (neutral(event)) axes.remove(event.deviceId)
            else axes.getOrPut(event.deviceId) { Press(GAME, 0, ++nextOrder) }.blocked = true
        } else if (event.actionMasked == MotionEvent.ACTION_CANCEL)
            axes[event.deviceId]?.blocked = true
        active.get()?.updateDirection()
    }

    /** Called before gameplay dispatch. A press owned by a UI window cannot
     * become a game button if that window closes while the key is still held. */
    fun consumeGameKey(event: KeyEvent): Boolean {
        if (!isNavigationKey(event.keyCode)) return false
        val id = event.deviceId to event.keyCode
        val previous = keys[id]
        if (event.action == KeyEvent.ACTION_UP) {
            val consumed = previous != null && (previous.owner != GAME || previous.blocked)
            if (event.isCanceled) previous?.blocked = true else keys.remove(id)
            active.get()?.updateDirection()
            return consumed
        }
        if (event.action != KeyEvent.ACTION_DOWN) return false
        usingController = true
        if (previous != null && (previous.owner != GAME || previous.blocked)) return true
        if (previous == null) keys[id] = Press(GAME, 0, ++nextOrder)
        return false
    }

    /** A held menu stick must not move the game on resume, but its report can
     * also contain a fresh trigger or right-stick input. Preserve those axes. */
    fun dispatchGameMotion(event: MotionEvent, dispatch: (MotionEvent) -> Boolean): Boolean {
        if (!filterGameMotion(event)) return dispatch(event)
        val properties = Array(event.pointerCount) { index ->
            MotionEvent.PointerProperties().also { event.getPointerProperties(index, it) }
        }
        val coordinates = Array(event.pointerCount) { index ->
            MotionEvent.PointerCoords().also {
                event.getPointerCoords(index, it)
                it.setAxisValue(MotionEvent.AXIS_X, 0f)
                it.setAxisValue(MotionEvent.AXIS_Y, 0f)
                it.setAxisValue(MotionEvent.AXIS_HAT_X, 0f)
                it.setAxisValue(MotionEvent.AXIS_HAT_Y, 0f)
            }
        }
        val filtered = MotionEvent.obtain(event.downTime, event.eventTime, event.action, event.pointerCount,
            properties, coordinates, event.metaState, event.buttonState, event.xPrecision, event.yPrecision,
            event.deviceId, event.edgeFlags, event.source, event.flags)
        try { return dispatch(filtered) } finally { filtered.recycle() }
    }

    private fun filterGameMotion(event: MotionEvent): Boolean {
        if (!isJoystick(event)) return false
        val previous = axes[event.deviceId]
        if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
            val consumed = previous != null && (previous.owner != GAME || previous.blocked)
            previous?.blocked = true
            return consumed
        }
        if (event.actionMasked != MotionEvent.ACTION_MOVE) return false
        if (neutral(event)) {
            axes.remove(event.deviceId)
            active.get()?.updateDirection()
            return previous != null && (previous.owner != GAME || previous.blocked)
        }
        usingController = true
        if (previous != null && (previous.owner != GAME || previous.blocked)) return true
        if (previous == null) axes[event.deviceId] = Press(GAME, 0, ++nextOrder)
        return false
    }

    fun gameInputPaused() {
        keys.values.filter { it.owner == GAME }.forEach { it.blocked = true }
        axes.values.filter { it.owner == GAME }.forEach { it.blocked = true }
    }

    fun removeDevice(device: Int) {
        keys.keys.removeAll { it.first == device }
        axes.remove(device)
        active.get()?.updateDirection()
    }

    private fun isJoystick(event: MotionEvent) =
        event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.isFromSource(InputDevice.SOURCE_GAMEPAD)

    private fun neutral(event: MotionEvent) =
        listOf(MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y)
            .all { abs(event.getAxisValue(it)) <= RELEASE }

    private fun hasNavigationAxis(event: MotionEvent) =
        event.getAxisValue(MotionEvent.AXIS_X) != 0f || event.getAxisValue(MotionEvent.AXIS_Y) != 0f ||
            event.getAxisValue(MotionEvent.AXIS_HAT_X) != 0f || event.getAxisValue(MotionEvent.AXIS_HAT_Y) != 0f

    private fun isDirection(code: Int) = code == KeyEvent.KEYCODE_DPAD_UP || code == KeyEvent.KEYCODE_DPAD_DOWN ||
        code == KeyEvent.KEYCODE_DPAD_LEFT || code == KeyEvent.KEYCODE_DPAD_RIGHT

    private fun isNavigationKey(code: Int) = isDirection(code) || code in listOf(
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_BACK,
        KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_Z,
    )

    private fun direction(event: MotionEvent, previous: Int): Int {
        val hx = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val hat = abs(hx) > RELEASE || abs(hy) > RELEASE
        val x = if (hat) hx else event.getAxisValue(MotionEvent.AXIS_X)
        val y = if (hat) hy else event.getAxisValue(MotionEvent.AXIS_Y)
        if (maxOf(abs(x), abs(y)) >= PRESS)
            return if (abs(x) > abs(y)) {
                if (x < 0) KeyEvent.KEYCODE_DPAD_LEFT else KeyEvent.KEYCODE_DPAD_RIGHT
            } else if (y < 0) KeyEvent.KEYCODE_DPAD_UP else KeyEvent.KEYCODE_DPAD_DOWN
        val held = when (previous) {
            KeyEvent.KEYCODE_DPAD_UP -> -y
            KeyEvent.KEYCODE_DPAD_DOWN -> y
            KeyEvent.KEYCODE_DPAD_LEFT -> -x
            KeyEvent.KEYCODE_DPAD_RIGHT -> x
            else -> 0f
        }
        return if (held > RELEASE) previous else 0
    }

    private class NavigationCallback(
        private val window: Window,
        private val previous: Window.Callback,
        var ensureFocus: () -> Boolean,
    ) : Window.Callback by previous {
        private val owner = ++nextOwner
        var forceFocus = false
        private var repeating = 0
        private val repeat = object : Runnable {
            override fun run() {
                if (!isFocused() || active.get() !== this@NavigationCallback || repeating == 0) {
                    stopRepeating()
                    return
                }
                send(repeating)
                if (repeating != 0) main.postDelayed(this, REPEAT_INTERVAL)
            }
        }

        init {
            window.decorView.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
                override fun onViewAttachedToWindow(view: View) = prepareIfFocused()
                override fun onViewDetachedFromWindow(view: View) = deactivate()
            })
        }

        private fun isFocused() = window.decorView.isAttachedToWindow && window.decorView.hasWindowFocus()

        fun prepareIfFocused() {
            if (!isFocused() || (!usingController && !forceFocus)) return
            activate()
            if (ensureFocus() || window.currentFocus != null) forceFocus = false
        }

        override fun onWindowFocusChanged(hasFocus: Boolean) {
            previous.onWindowFocusChanged(hasFocus)
            if (hasFocus) prepareIfFocused() else deactivate()
        }

        private fun activate() {
            if (active.get() === this) return
            active.get()?.deactivate()
            active = WeakReference(this)
        }

        private fun deactivate() {
            stopRepeating()
            keys.values.filter { it.owner == owner }.forEach { it.blocked = true }
            axes.values.filter { it.owner == owner }.forEach { it.blocked = true }
            if (active.get() === this) active.clear()
        }

        override fun dispatchTouchEvent(event: MotionEvent): Boolean {
            if (event.actionMasked == MotionEvent.ACTION_DOWN) {
                usingController = false
                deactivate()
            }
            return previous.dispatchTouchEvent(event)
        }

        private fun action(event: KeyEvent): Int {
            val editing = window.currentFocus?.onCheckIsTextEditor() == true
            return when (event.keyCode) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT ->
                    if (editing && event.isFromSource(InputDevice.SOURCE_KEYBOARD)) TEXT else event.keyCode
                KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE -> BACK
                KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> if (editing) TEXT else CONFIRM
                KeyEvent.KEYCODE_DPAD_CENTER -> CONFIRM
                KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_Z -> {
                    if (editing || event.isCtrlPressed || event.isAltPressed || event.isMetaPressed) TEXT
                    else if (event.keyCode == KeyEvent.KEYCODE_X) CONFIRM else BACK
                }
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B -> {
                    val label = PreferenceManager.getDefaultSharedPreferences(window.context)
                        .getString("gamepad_mapping", "position") == "label"
                    if ((event.keyCode == KeyEvent.KEYCODE_BUTTON_A) == label) CONFIRM else BACK
                }
                else -> 0
            }
        }

        override fun dispatchKeyEvent(event: KeyEvent): Boolean {
            GameActivity.observePausedKeyEvent(event)
            if (!isNavigationKey(event.keyCode) ||
                (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP))
                return previous.dispatchKeyEvent(event)
            val id = event.deviceId to event.keyCode
            val held = keys[id]
            if (event.action == KeyEvent.ACTION_UP) {
                val valid = held?.owner == owner && !held.blocked && !event.isCanceled
                if (event.isCanceled) held?.blocked = true else keys.remove(id)
                active.get()?.updateDirection()
                if (held?.owner == owner) {
                    if (held.action == TEXT) return previous.dispatchKeyEvent(
                        if (valid) event else KeyEvent.changeFlags(event, event.flags or KeyEvent.FLAG_CANCELED))
                    if (held.action == CONFIRM || held.action == BACK)
                        dispatchAction(event, held.action, canceled = !valid)
                }
                return true
            }
            val firstControllerInput = !usingController
            usingController = true
            activate()
            if (held != null) return if (held.owner == owner && !held.blocked && held.action == TEXT && action(event) == TEXT)
                previous.dispatchKeyEvent(event) else true
            val selected = action(event)
            val press = Press(owner, selected, ++nextOrder, blocked = event.repeatCount > 0)
            keys[id] = press
            if (press.blocked) return true
            if (selected == TEXT) return previous.dispatchKeyEvent(event)
            val focused = ensureFocus()
            if (isDirection(selected)) updateDirection(skipFirst = focused && firstControllerInput)
            else {
                stopRepeating()
                // A confirm/back may replace a page inside this same Window.
                // Its UP must not restart the previous page's held direction.
                keys.values.filter { it.owner == owner && isDirection(it.action) }.forEach { it.blocked = true }
                axes.values.filter { it.owner == owner }.forEach { it.blocked = true }
                // Keep the native DOWN/UP pair. Activating on DOWN destroys
                // the old input window before a quick physical UP arrives;
                // Android can drop that release during an Activity handoff.
                // Native release activation also preserves canceled presses.
                dispatchAction(event, selected)
            }
            return true
        }

        override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
            GameActivity.observePausedMotionEvent(event)
            if (!isJoystick(event)) return previous.dispatchGenericMotionEvent(event)
            val held = axes[event.deviceId]
            if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
                held?.blocked = true
                updateDirection()
                return previous.dispatchGenericMotionEvent(event)
            }
            if (event.actionMasked != MotionEvent.ACTION_MOVE) return previous.dispatchGenericMotionEvent(event)
            if (neutral(event)) {
                axes.remove(event.deviceId)
                updateDirection()
                // Trigger-only neutral samples still reach focused children.
                return if (hasNavigationAxis(event)) true else previous.dispatchGenericMotionEvent(event)
            }
            val firstControllerInput = !usingController
            usingController = true
            activate()
            if (held != null && (held.owner != owner || held.blocked)) return true
            val next = direction(event, held?.action ?: 0)
            if (held == null || next != held.action)
                axes[event.deviceId] = Press(owner, next, ++nextOrder)
            val focused = ensureFocus()
            updateDirection(skipFirst = focused && firstControllerInput)
            return true
        }

        fun updateDirection(skipFirst: Boolean = false) {
            val next = (keys.values.asSequence() + axes.values.asSequence())
                .filter { it.owner == owner && !it.blocked && isDirection(it.action) }
                .maxByOrNull { it.order }?.action ?: 0
            if (next == repeating) return
            stopRepeating()
            if (next == 0 || active.get() !== this || !isFocused()) return
            repeating = next
            if (!skipFirst) send(next)
            if (repeating != 0) main.postDelayed(repeat, REPEAT_DELAY)
        }

        private fun send(code: Int) {
            val now = SystemClock.uptimeMillis()
            val handled = previous.dispatchKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0))
            if (!handled && isDirection(code)) {
                // Real keys fall through to ViewRoot's focus navigation.
                // These normalized keys bypass that outer stage, so retain
                // the same fallback after native ListView/SeekBar handling.
                val direction = when (code) {
                    KeyEvent.KEYCODE_DPAD_UP -> View.FOCUS_UP
                    KeyEvent.KEYCODE_DPAD_DOWN -> View.FOCUS_DOWN
                    KeyEvent.KEYCODE_DPAD_LEFT -> View.FOCUS_LEFT
                    else -> View.FOCUS_RIGHT
                }
                val focused = window.currentFocus
                val next = (focused ?: window.decorView).focusSearch(direction)
                if (next == null || next === focused || !next.isEnabled || !next.requestFocus(direction))
                    window.decorView.dispatchUnhandledMove(focused, direction)
            }
            previous.dispatchKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_UP, code, 0))
        }

        private fun dispatchAction(event: KeyEvent, action: Int, canceled: Boolean = false) {
            val code = if (action == CONFIRM) KeyEvent.KEYCODE_DPAD_CENTER else KeyEvent.KEYCODE_BACK
            previous.dispatchKeyEvent(KeyEvent(event.downTime, event.eventTime, event.action, code,
                event.repeatCount, event.metaState, event.deviceId, event.scanCode,
                event.flags or (if (canceled) KeyEvent.FLAG_CANCELED else 0), event.source))
        }

        private fun stopRepeating() {
            main.removeCallbacks(repeat)
            repeating = 0
        }
    }
}
