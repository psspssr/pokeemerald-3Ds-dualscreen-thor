package com.emerald3ds.android

import android.graphics.PointF
import android.graphics.RectF
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.KeyEvent
import android.widget.FrameLayout
import androidx.test.core.app.ActivityScenario
import androidx.lifecycle.Lifecycle
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Assume.assumeTrue
import androidx.preference.PreferenceManager
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Injects real multi-pointer touch events (through UiAutomation, like a
 * finger) and checks the input sent to the separate native host harness.
 * Never writes a placeholder data pack into the real game's storage.
 */
@RunWith(AndroidJUnit4::class)
class ControlsInputTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()

    @Before
    fun prepare() {
        assumeTrue(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(instrumentation.targetContext).edit()
            .clear().putBoolean("dual_display", false).putString("controls_visibility", "always").commit()
        instrumentation.runOnMainSync { InputHub.clear() }
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
                ready = view.inputEnabled && view.controlsVisible &&
                    view.bottomScreenBounds() != null && !view.controlBounds(0).isEmpty
            }
            if (ready) return
            SystemClock.sleep(100)
        }
        throw AssertionError("game input overlay was not ready")
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

    @Test
    fun choosingAlwaysRestoresControlsHiddenByPhysicalInput() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(instrumentation.targetContext)
        prefs.edit().putString("controls_visibility", "auto").commit()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitForLayout(overlay(scenario))
            scenario.onActivity {
                it.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_Z))
                it.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_Z))
            }
            instrumentation.waitForIdleSync()
            assertTrue(overlay(scenario).autoHidden)
            scenario.moveToState(Lifecycle.State.CREATED)
            prefs.edit().putString("controls_visibility", "always").commit()
            scenario.moveToState(Lifecycle.State.RESUMED)
            waitForLayout(overlay(scenario))
            assertTrue(overlay(scenario).controlsVisible)
        }
    }

    @Test
    fun centeredVirtualCircleKeepsOwnershipUntilFingerRelease() {
        PreferenceManager.getDefaultSharedPreferences(instrumentation.targetContext)
            .edit().putBoolean("circle_pad", true).commit()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            val view = overlay(scenario)
            waitForLayout(view)
            lateinit var bounds: RectF
            instrumentation.runOnMainSync {
                bounds = view.circlePadBounds()
                InputHub.setCircle(InputHub.SRC_AXES, CtrKeys.CIRCLE_MAX, 0)
            }
            assertTrue(!bounds.isEmpty)
            assertEquals(CtrKeys.CPAD_RIGHT, sentKeys())
            val center = screen(view, bounds.centerX(), bounds.centerY())
            val left = screen(view, bounds.left + bounds.width() * 0.1f, bounds.centerY())
            val down = SystemClock.uptimeMillis()
            try {
                inject(down, MotionEvent.ACTION_DOWN, listOf(center))
                assertEquals("centered virtual stick must override the held physical stick", 0, sentKeys())
                inject(down, MotionEvent.ACTION_MOVE, listOf(left))
                assertEquals(CtrKeys.CPAD_LEFT, sentKeys())
                inject(down, MotionEvent.ACTION_MOVE, listOf(center))
                assertEquals(0, sentKeys())
            } finally {
                inject(down, MotionEvent.ACTION_UP, listOf(center))
            }
            assertEquals("physical stick resumes after the virtual finger releases", CtrKeys.CPAD_RIGHT, sentKeys())
            instrumentation.runOnMainSync { InputHub.clear() }
        }
    }
}
