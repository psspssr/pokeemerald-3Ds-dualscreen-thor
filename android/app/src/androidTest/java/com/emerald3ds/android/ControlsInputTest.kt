package com.emerald3ds.android

import android.graphics.PointF
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.widget.FrameLayout
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Injects real multi-pointer touch events (through UiAutomation, like a
 * finger) and checks the input sent to the game. Needs the data pack check to
 * pass, so it writes a placeholder pack when none is present.
 */
@RunWith(AndroidJUnit4::class)
class ControlsInputTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()

    @Before
    fun preparePak() {
        val files = GameFiles(instrumentation.targetContext)
        files.ensureDirs()
        if (!files.pakFile.isFile) {
            files.pakFile.writeBytes(byteArrayOf(0x45, 0x4D, 0x33, 0x44, 0x50, 0x41, 0x4B, 0x00) + ByteArray(120))
        }
    }

    private fun overlay(scenario: ActivityScenario<GameActivity>): ControlsOverlayView {
        lateinit var view: ControlsOverlayView
        scenario.onActivity { view = it.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView }
        return view
    }

    private fun screen(view: ControlsOverlayView, x: Float, y: Float): PointF {
        val loc = IntArray(2)
        instrumentation.runOnMainSync { view.getLocationOnScreen(loc) }
        return PointF(loc[0] + x, loc[1] + y)
    }

    private fun inject(downTime: Long, action: Int, points: List<PointF>, ids: List<Int> = points.indices.toList()) {
        val props = Array(points.size) { i ->
            MotionEvent.PointerProperties().apply {
                id = ids[i]
                toolType = MotionEvent.TOOL_TYPE_FINGER
            }
        }
        val coords = Array(points.size) { i ->
            MotionEvent.PointerCoords().apply {
                x = points[i].x
                y = points[i].y
                pressure = 1f
                size = 1f
            }
        }
        val event = MotionEvent.obtain(
            downTime, SystemClock.uptimeMillis(), action, points.size, props, coords,
            0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0,
        )
        assertTrue(instrumentation.uiAutomation.injectInputEvent(event, true))
        event.recycle()
        instrumentation.waitForIdleSync()
    }

    private fun sentKeys(): Int {
        var keys = 0
        instrumentation.runOnMainSync { keys = InputHub.sentKeys }
        return keys
    }

    private fun waitForLayout(view: ControlsOverlayView) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            var ready = false
            instrumentation.runOnMainSync {
                ready = view.controlsVisible && view.bottomScreenBounds() != null && !view.controlBounds(0).isEmpty
            }
            if (ready) return
            SystemClock.sleep(100)
        }
        throw AssertionError("overlay not laid out")
    }

    @Test
    fun dpadAndTouchScreenTogether() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            val view = overlay(scenario)
            waitForLayout(view)
            val dpad = view.controlBounds(0)
            val bottom = view.bottomScreenBounds()!!
            val left = screen(view, dpad.left + dpad.width() * 0.15f, dpad.centerY())
            val touch = screen(view, bottom.left + bottom.width() * 0.75f, bottom.top + bottom.height() * 0.5f)
            val t0 = SystemClock.uptimeMillis()

            inject(t0, MotionEvent.ACTION_DOWN, listOf(left))
            assertEquals(CtrKeys.DLEFT, sentKeys())

            inject(t0, MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), listOf(left, touch))
            assertEquals(CtrKeys.DLEFT or CtrKeys.TOUCH, sentKeys())
            instrumentation.runOnMainSync {
                assertEquals(240, InputHub.sentTouchX)
                assertEquals(120, InputHub.sentTouchY)
            }

            /* The D-pad finger slides to up-left while the stylus finger moves. */
            val upLeft = screen(view, dpad.left + dpad.width() * 0.2f, dpad.top + dpad.height() * 0.2f)
            val touch2 = screen(view, bottom.left + bottom.width() * 0.25f, bottom.top + bottom.height() * 0.25f)
            inject(t0, MotionEvent.ACTION_MOVE, listOf(upLeft, touch2))
            assertEquals(CtrKeys.DLEFT or CtrKeys.DUP or CtrKeys.TOUCH, sentKeys())
            instrumentation.runOnMainSync {
                assertEquals(80, InputHub.sentTouchX)
                assertEquals(60, InputHub.sentTouchY)
            }

            inject(t0, MotionEvent.ACTION_POINTER_UP or (0 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), listOf(upLeft, touch2))
            assertEquals(CtrKeys.TOUCH, sentKeys())
            inject(t0, MotionEvent.ACTION_UP, listOf(touch2), ids = listOf(1))
            assertEquals(0, sentKeys())
        }
    }

    @Test
    fun twoButtonsAndSlide() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            val view = overlay(scenario)
            waitForLayout(view)
            val a = view.controlBounds(CtrKeys.A)
            val b = view.controlBounds(CtrKeys.B)
            val l = view.controlBounds(CtrKeys.L)
            val pa = screen(view, a.centerX(), a.centerY())
            val pb = screen(view, b.centerX(), b.centerY())
            val pl = screen(view, l.centerX(), l.centerY())
            val t0 = SystemClock.uptimeMillis()
            inject(t0, MotionEvent.ACTION_DOWN, listOf(pl))
            inject(t0, MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), listOf(pl, pa))
            assertEquals(CtrKeys.L or CtrKeys.A, sentKeys())
            inject(t0, MotionEvent.ACTION_MOVE, listOf(pl, pb))
            assertEquals(CtrKeys.L or CtrKeys.B, sentKeys())
            inject(t0, MotionEvent.ACTION_POINTER_UP or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), listOf(pl, pb))
            assertEquals(CtrKeys.L, sentKeys())
            inject(t0, MotionEvent.ACTION_UP, listOf(pl))
            assertEquals(0, sentKeys())
        }
    }
}
