package com.emerald3ds.android

import android.graphics.PointF
import android.graphics.Rect
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class InputOwnershipTest {
    private val inst = InstrumentationRegistry.getInstrumentation()

    @Before fun reset() {
        check(BuildConfig.HOST_HARNESS)
        inst.runOnMainSync { InputHub.clear() }
    }

    @After fun cleanup() = inst.runOnMainSync { InputHub.clear() }

    private class Owner : PhysicalInput.Callbacks {
        val input = PhysicalInput(this) { false }
        var toggles = 0
        var held = false
        override fun onPhysicalInput() {}
        override fun onMenuKey() {}
        override fun onToggleBottomScreen() {}
        override fun onFastForwardToggle() { toggles++ }
        override fun onFastForwardHold(held: Boolean) { this.held = held }
    }

    private fun motion(input: PhysicalInput, device: Int, x: Float = 0f, y: Float = 0f,
        hatX: Float = 0f, hatY: Float = 0f, rightX: Float = 0f, leftTrigger: Float = 0f,
        rightTrigger: Float = 0f, action: Int = MotionEvent.ACTION_MOVE) {
        val now = SystemClock.uptimeMillis()
        val event = MotionEvent.obtain(now, now, action, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
            arrayOf(MotionEvent.PointerCoords().apply {
                setAxisValue(MotionEvent.AXIS_X, x); setAxisValue(MotionEvent.AXIS_Y, y)
                setAxisValue(MotionEvent.AXIS_HAT_X, hatX); setAxisValue(MotionEvent.AXIS_HAT_Y, hatY)
                setAxisValue(MotionEvent.AXIS_Z, rightX)
                setAxisValue(MotionEvent.AXIS_LTRIGGER, leftTrigger)
                setAxisValue(MotionEvent.AXIS_RTRIGGER, rightTrigger)
            }), 0, 0, 1f, 1f, device, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try { assertTrue("joystick event was not handled", input.onMotion(event)) } finally { event.recycle() }
    }

    private fun key(input: PhysicalInput, device: Int, code: Int, action: Int) {
        val now = SystemClock.uptimeMillis()
        assertTrue(input.onKey(KeyEvent(now, now, action, code, 0, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD)))
    }

    private fun overlay(): ControlsOverlayView = ControlsOverlayView(inst.targetContext, InputHub.SRC_OVERLAY, true).apply {
        layout(0, 0, 1600, 1000)
        configure(AppSettings.load(context).copy(circlePad = true, haptics = false, controlsScale = 0.5f),
            Rect(0, 0, 800, 480), Rect(800, 0, 1600, 600), Rect(), showToggle = false, allowed = true)
        inputEnabled = true
    }

    private fun touch(view: ControlsOverlayView, started: Long, action: Int, vararg points: Pair<Int, PointF>) {
        val event = MotionEvent.obtain(started, SystemClock.uptimeMillis(), action, points.size,
            points.map { (id, _) -> MotionEvent.PointerProperties().apply { this.id = id; toolType = MotionEvent.TOOL_TYPE_FINGER } }.toTypedArray(),
            points.map { (_, point) -> MotionEvent.PointerCoords().apply { x = point.x; y = point.y; pressure = 1f; size = 1f } }.toTypedArray(),
            0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
        try { assertTrue(view.onTouchEvent(event)) } finally { event.recycle() }
    }

    @Test fun replacementPrimaryDownCannotLeavePreviousTouchOrButtonsLatched() {
        inst.runOnMainSync {
            val view = overlay()
            val a = view.controlBounds(CtrKeys.A).let { PointF(it.centerX(), it.centerY()) }
            val l = view.controlBounds(CtrKeys.L).let { PointF(it.centerX(), it.centerY()) }
            val bottom = PointF(1200f, 300f)
            InputHub.setKeys(InputHub.SRC_GAMEPAD, CtrKeys.B)
            InputHub.setCircle(InputHub.SRC_AXES, CtrKeys.CIRCLE_MAX, 0)
            val external = CtrKeys.B or CtrKeys.CPAD_RIGHT
            val first = SystemClock.uptimeMillis()
            touch(view, first, MotionEvent.ACTION_DOWN, 0 to bottom)
            touch(view, first, MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 0 to bottom, 1 to l)
            assertEquals(external or CtrKeys.TOUCH or CtrKeys.L, InputHub.sentKeys)
            // Android's consistency contract explicitly permits a new DOWN
            // after an old gesture's UP/CANCEL was dropped by a containing view.
            SystemClock.sleep(2)
            val replacement = SystemClock.uptimeMillis()
            touch(view, replacement, MotionEvent.ACTION_DOWN, 0 to a)
            val duringReplacement = InputHub.sentKeys
            touch(view, replacement, MotionEvent.ACTION_UP, 0 to a)
            assertEquals("completed replacement gesture left a native touch stuck", external, InputHub.sentKeys)
            assertEquals("new gesture retained buttons from the old gesture", external or CtrKeys.A, duringReplacement)
        }
    }

    @Test fun touchCancelAndReplacementKeepAnotherWindowsTouchAndPhysicalStick() {
        inst.runOnMainSync {
            val view = overlay()
            val circle = view.circlePadBounds().let { PointF(it.centerX(), it.centerY()) }
            val a = view.controlBounds(CtrKeys.A).let { PointF(it.centerX(), it.centerY()) }
            InputHub.setTouch(InputHub.SRC_PRESENTATION, true, 210, 90)
            InputHub.setKeys(InputHub.SRC_KEYBOARD, CtrKeys.SELECT)
            InputHub.setCircle(InputHub.SRC_AXES, CtrKeys.CIRCLE_MAX, 0)
            val external = CtrKeys.TOUCH or CtrKeys.SELECT
            val first = SystemClock.uptimeMillis()
            touch(view, first, MotionEvent.ACTION_DOWN, 0 to circle)
            assertEquals(external, InputHub.sentKeys)
            touch(view, first, MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 0 to circle, 1 to a)
            assertEquals(external or CtrKeys.A, InputHub.sentKeys)
            touch(view, first, MotionEvent.ACTION_CANCEL, 0 to circle, 1 to a)
            assertEquals(external or CtrKeys.CPAD_RIGHT, InputHub.sentKeys)
            touch(view, first, MotionEvent.ACTION_DOWN, 0 to a)
            touch(view, first, MotionEvent.ACTION_DOWN, 0 to PointF(1200f, 300f))
            touch(view, first, MotionEvent.ACTION_UP, 0 to PointF(1200f, 300f))
            assertEquals(external or CtrKeys.CPAD_RIGHT, InputHub.sentKeys)
            assertEquals(210, InputHub.sentTouchX)
            assertEquals(90, InputHub.sentTouchY)
        }
    }

    @Test fun neutralControllerCannotEraseAnotherControllersHeldMotion() {
        inst.runOnMainSync {
            val input = Owner().input
            motion(input, 11, x = 1f, hatX = -1f, rightX = 1f, rightTrigger = 1f)
            val first = CtrKeys.CPAD_RIGHT or CtrKeys.DLEFT or CtrKeys.CSTICK_RIGHT or CtrKeys.ZR
            assertEquals(first, InputHub.sentKeys)
            motion(input, 22)
            assertEquals("neutral controller erased another controller's held axes", first, InputHub.sentKeys)
            motion(input, 22, hatY = 1f, leftTrigger = 1f)
            assertEquals(first or CtrKeys.DDOWN or CtrKeys.ZL, InputHub.sentKeys)
            motion(input, 11)
            assertEquals(CtrKeys.DDOWN or CtrKeys.ZL, InputHub.sentKeys)
            input.removeDevice(22)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun disconnectAndNeutralRestoreTheOtherHeldStick() {
        inst.runOnMainSync {
            val input = Owner().input
            motion(input, 11, x = 1f)
            motion(input, 22, x = -1f)
            assertEquals(CtrKeys.CPAD_LEFT, InputHub.sentKeys)
            input.removeDevice(22)
            assertEquals("disconnect lost the still-held controller", CtrKeys.CPAD_RIGHT, InputHub.sentKeys)
            motion(input, 22, x = -1f)
            motion(input, 22)
            assertEquals("neutralizing one stick lost the other", CtrKeys.CPAD_RIGHT, InputHub.sentKeys)
            motion(input, 11)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun unrelatedHatUpdatesCannotStealTheMostRecentlyMovedStick() {
        inst.runOnMainSync {
            val input = Owner().input
            motion(input, 11, x = 1f)
            motion(input, 22, x = -1f)
            motion(input, 11, x = 1f, hatY = -1f)
            assertEquals(CtrKeys.CPAD_LEFT or CtrKeys.DUP, InputHub.sentKeys)
            motion(input, 11, x = 0.8f, hatY = -1f)
            assertEquals(CtrKeys.CPAD_RIGHT or CtrKeys.DUP, InputHub.sentKeys)
            input.removeDevice(11)
            assertEquals(CtrKeys.CPAD_LEFT, InputHub.sentKeys)
        }
    }

    @Test fun motionCancelReleasesOnlyItsDeviceAndPreservesTriggerReleaseGuards() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input.apply { fastForwardEnabled = true }
            key(input, 11, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.ACTION_DOWN)
            motion(input, 11, x = 1f, hatX = 1f, leftTrigger = 1f, rightTrigger = 1f)
            motion(input, 22, x = -1f, hatX = -1f)
            key(input, 22, KeyEvent.KEYCODE_BUTTON_A, KeyEvent.ACTION_DOWN)
            assertEquals(1, owner.toggles)
            assertTrue(owner.held)
            motion(input, 11, action = MotionEvent.ACTION_CANCEL)
            assertEquals(CtrKeys.B or CtrKeys.DLEFT or CtrKeys.CPAD_LEFT, InputHub.sentKeys)
            assertFalse(owner.held)
            assertEquals(1, owner.toggles)
            // Cancel is not proof that the physical R2 was released.
            motion(input, 11, rightTrigger = 0.39f)
            key(input, 11, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.ACTION_DOWN)
            assertEquals(1, owner.toggles)
            key(input, 11, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.ACTION_UP)
            assertEquals(1, owner.toggles)
            key(input, 11, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.ACTION_DOWN)
            motion(input, 11, rightTrigger = 0.6f)
            assertEquals(2, owner.toggles)
            input.clear()
        }
    }

    @Test fun clearingPhysicalInputDropsEveryMotionCacheButPreservesOtherSources() {
        inst.runOnMainSync {
            val input = Owner().input
            InputHub.setKeys(InputHub.SRC_OVERLAY, CtrKeys.A)
            InputHub.setTouch(InputHub.SRC_PRESENTATION, true, 30, 40)
            motion(input, 11, x = 1f, hatY = -1f)
            motion(input, 22, x = -1f, rightX = 1f)
            input.clear()
            assertEquals(CtrKeys.A or CtrKeys.TOUCH, InputHub.sentKeys)
            motion(input, 22)
            assertEquals("an old controller state reappeared after clear", CtrKeys.A or CtrKeys.TOUCH, InputHub.sentKeys)
            motion(input, 11, x = 1f)
            assertEquals(CtrKeys.A or CtrKeys.TOUCH or CtrKeys.CPAD_RIGHT, InputHub.sentKeys)
            input.removeDevice(11)
            assertEquals(CtrKeys.A or CtrKeys.TOUCH, InputHub.sentKeys)
        }
    }
}
