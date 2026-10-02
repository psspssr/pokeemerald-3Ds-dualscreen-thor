package com.emerald3ds.android

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.PixelCopy
import android.view.SurfaceView
import android.widget.FrameLayout
import androidx.lifecycle.Lifecycle
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real Android display/window, EGL, JNI and touch tests, using the isolated host harness. */
@RunWith(AndroidJUnit4::class)
class DualDisplayTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val context = inst.targetContext
    private var display: VirtualDisplay? = null
    private var reader: ImageReader? = null

    @Before fun setup() {
        check(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(context).edit().clear()
            .putBoolean("dual_display", true).commit()
        inst.runOnMainSync { InputHub.clear() }
    }

    @After fun cleanup() { removeDisplay() }

    private fun addDisplay() {
        inst.runOnMainSync {
            val r = ImageReader.newInstance(1240, 1080, PixelFormat.RGBA_8888, 3)
            r.setOnImageAvailableListener({ it.acquireLatestImage()?.close() }, Handler(Looper.getMainLooper()))
            reader = r
            display = context.getSystemService(DisplayManager::class.java).createVirtualDisplay(
                "Thor bottom test", 1240, 1080, 240, r.surface,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC or
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION or
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY,
            )
            checkNotNull(display)
        }
    }

    private fun removeDisplay() {
        inst.runOnMainSync {
            display?.release()
            display = null
            reader?.close()
            reader = null
        }
    }

    private fun waitUntil(message: String, condition: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < end) {
            if (condition()) return
            SystemClock.sleep(50)
        }
        fail(message)
    }

    private fun presentation(scenario: ActivityScenario<GameActivity>): GamePresentation? {
        var p: GamePresentation? = null
        scenario.onActivity { p = it.presentation }
        return p
    }

    private fun waitDual(scenario: ActivityScenario<GameActivity>) {
        waitUntil("second display was not rendered") {
            val s = HostProbe.snapshot()
            presentation(scenario)?.surfaceWidth == 1240 && s[7] == 1 && s[17] > 0
        }
    }

    private fun snapshot(surface: SurfaceView, name: String): Bitmap {
        val result = Bitmap.createBitmap(surface.width, surface.height, Bitmap.Config.ARGB_8888)
        val latch = CountDownLatch(1)
        var status = -1
        PixelCopy.request(surface, result, { status = it; latch.countDown() }, Handler(Looper.getMainLooper()))
        assertTrue("PixelCopy timed out", latch.await(5, TimeUnit.SECONDS))
        assertEquals(PixelCopy.SUCCESS, status)
        val file = File(context.getExternalFilesDir(null), "test-evidence/$name.png")
        file.parentFile!!.mkdirs()
        file.outputStream().use { result.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return result
    }

    private fun touch(displayId: Int, action: Int, x: Float, y: Float, down: Long) {
        val e = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, x, y, 0)
        e.source = InputDevice.SOURCE_TOUCHSCREEN
        // setDisplayId is hidden in the SDK, but available to debuggable instrumentation.
        android.view.InputEvent::class.java.getMethod("setDisplayId", Int::class.javaPrimitiveType)
            .invoke(e, displayId)
        assertTrue(inst.uiAutomation.injectInputEvent(e, true))
        e.recycle()
        inst.waitForIdleSync()
    }

    @Test fun bothWindowsRenderAndBottomTouchReachesNative() {
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitDual(scenario)
            val p = presentation(scenario)!!
            val state = HostProbe.snapshot()
            assertArrayEquals(intArrayOf(0, 75, 1240, 930), state.sliceArray(12..15))
            scenario.onActivity {
                val overlay = it.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView
                assertFalse(overlay.controlsVisible)
            }
            lateinit var main: SurfaceView
            scenario.onActivity { main = it.findViewById(R.id.game_surface) }
            val topImage = snapshot(main, "thor-top")
            assertEquals(Color.rgb(30, 74, 168), topImage.getPixel(state[8] + 20, state[9] + state[11] / 2))
            val bottomImage = snapshot(p.surfaceView, "thor-bottom")
            assertEquals(Color.rgb(46, 139, 87), bottomImage.getPixel(620, 540))
            assertEquals(Color.BLACK, bottomImage.getPixel(620, 10))
            val t = SystemClock.uptimeMillis()
            touch(display!!.display.displayId, MotionEvent.ACTION_DOWN, 930f, 540f, t)
            waitUntil("secondary touch did not reach native host") { HostProbe.snapshot()[1] and CtrKeys.TOUCH != 0 }
            assertArrayEquals(intArrayOf(240, 120), HostProbe.snapshot().sliceArray(4..5))
            snapshot(p.surfaceView, "thor-bottom-touch")
            touch(display!!.display.displayId, MotionEvent.ACTION_UP, 930f, 540f, t)
            assertEquals(0, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
            val before = HostProbe.snapshot()
            SystemClock.sleep(500)
            val after = HostProbe.snapshot()
            assertTrue("main display stalled", after[16] > before[16] + 5)
            assertTrue("bottom display stalled", after[17] > before[17] + 5)
        }
    }

    @Test fun displayLossFallsBackReattachesAndPauseStopsDrawing() {
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitDual(scenario)
            val t = SystemClock.uptimeMillis()
            touch(display!!.display.displayId, MotionEvent.ACTION_DOWN, 620f, 540f, t)
            assertTrue(HostProbe.snapshot()[1] and CtrKeys.TOUCH != 0)
            removeDisplay()
            waitUntil("did not fall back after display loss") {
                presentation(scenario) == null && HostProbe.snapshot()[7] == 0
            }
            assertEquals(0, HostProbe.snapshot()[1])
            assertTrue(HostProbe.snapshot()[14] > 0)
            addDisplay()
            waitDual(scenario)
            scenario.moveToState(Lifecycle.State.CREATED)
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            SystemClock.sleep(100)
            val before = HostProbe.snapshot()
            SystemClock.sleep(250)
            assertArrayEquals(before.sliceArray(16..17), HostProbe.snapshot().sliceArray(16..17))
            scenario.moveToState(Lifecycle.State.RESUMED)
            waitDual(scenario)
            waitUntil("native renderer did not resume") { HostProbe.snapshot()[16] > before[16] }
        }
    }

    @Test fun reverseDisplaysAndIntegerScaling() {
        PreferenceManager.getDefaultSharedPreferences(context).edit()
            .putString("top_display", "second").putBoolean("integer_scaling", true).commit()
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitUntil("reverse display assignment failed") {
                presentation(scenario)?.surfaceWidth == 1240 && HostProbe.snapshot()[6] == 1
            }
            val s = HostProbe.snapshot()
            assertEquals(0, s[7])
            assertArrayEquals(intArrayOf(20, 180, 1200, 720), s.sliceArray(8..11))
            assertEquals(0, s[14] % 320)
            assertEquals(0, s[15] % 240)
        }
    }

    @Test fun positionalButtonsAndKeyboardReachNativeWithoutStuckInput() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitUntil("game not started") { NativeBridge.isStarted() }
            val pairs = mapOf(KeyEvent.KEYCODE_BUTTON_A to CtrKeys.B, KeyEvent.KEYCODE_BUTTON_B to CtrKeys.A,
                KeyEvent.KEYCODE_BUTTON_X to CtrKeys.Y, KeyEvent.KEYCODE_BUTTON_Y to CtrKeys.X,
                KeyEvent.KEYCODE_BUTTON_L1 to CtrKeys.L, KeyEvent.KEYCODE_BUTTON_R1 to CtrKeys.R)
            for ((code, key) in pairs) {
                scenario.onActivity { it.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, code)) }
                assertEquals(key, HostProbe.snapshot()[1])
                scenario.onActivity { it.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, code)) }
                assertEquals(0, HostProbe.snapshot()[1])
            }
            scenario.onActivity { it.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_Z)) }
            assertEquals(CtrKeys.B, HostProbe.snapshot()[1])
            scenario.moveToState(Lifecycle.State.CREATED)
            assertEquals(0, HostProbe.snapshot()[1])
        }
    }
}
