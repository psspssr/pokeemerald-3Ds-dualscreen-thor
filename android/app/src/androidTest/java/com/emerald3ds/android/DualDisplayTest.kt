package com.emerald3ds.android

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.Rect
import android.content.pm.ActivityInfo
import android.accessibilityservice.AccessibilityServiceInfo
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
import android.view.WindowManager
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.FrameLayout
import androidx.lifecycle.Lifecycle
import androidx.preference.PreferenceManager
import androidx.preference.ListPreference
import androidx.preference.SwitchPreferenceCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
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
            .putBoolean("dual_display", true).putString("dual_scaling", "fit").commit()
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
        val frame = HostProbe.snapshot()[17]
        waitUntil("new bottom surface has not drawn") { HostProbe.snapshot()[17] > frame + 1 }
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
        val pointer = MotionEvent.PointerProperties().apply { id = 0; toolType = MotionEvent.TOOL_TYPE_FINGER }
        val coords = MotionEvent.PointerCoords().apply { this.x = x; this.y = y; pressure = 1f; size = 1f }
        val e = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, 1,
            arrayOf(pointer), arrayOf(coords), 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
        // setDisplayId is hidden in the SDK, but available to debuggable instrumentation.
        android.view.InputEvent::class.java.getMethod("setDisplayId", Int::class.javaPrimitiveType)
            .invoke(e, displayId)
        assertTrue(inst.uiAutomation.injectInputEvent(e, true))
        e.recycle()
        inst.waitForIdleSync()
    }

    @Test fun fullPanelChoiceUsesBothEntireThorPanelsWithoutCropping() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        prefs.edit().putString("dual_scaling", "fill").commit()
        val fill = AppSettings.load(context)
        for (integer in listOf(false, true)) {
            val settings = fill.copy(integerScaling = integer)
            val normal = ScreenLayout.dual(1920, 1080, Rect(), 1240, 1080, settings)
            assertEquals(Rect(0, 0, 1920, 1080), normal.top)
            assertEquals(Rect(0, 0, 1240, 1080), normal.bottom)
            val swapped = ScreenLayout.dual(1920, 1080, Rect(), 1240, 1080,
                settings.copy(topOnSecondDisplay = true))
            assertEquals(Rect(0, 0, 1240, 1080), swapped.top)
            assertEquals(Rect(0, 0, 1920, 1080), swapped.bottom)
            assertEquals(NativeBridge.WINDOW_SECOND, swapped.topWindow)
            assertEquals(NativeBridge.WINDOW_MAIN, swapped.bottomWindow)
        }
        prefs.edit().putString("dual_scaling", "fit").commit()
        val fit = AppSettings.load(context)
        val nativeAspect = ScreenLayout.dual(1920, 1080, Rect(), 1240, 1080, fit)
        assertEquals(Rect(60, 0, 1860, 1080), nativeAspect.top)
        assertEquals(Rect(0, 75, 1240, 1005), nativeAspect.bottom)
        for ((width, height) in listOf(1080 to 1920, 1920 to 1080)) {
            val phoneFill = ScreenLayout.single(width, height, Rect(), fill, 250, true, false)
            val phoneFit = ScreenLayout.single(width, height, Rect(), fit, 250, true, false)
            assertEquals(phoneFit.top, phoneFill.top)
            assertEquals(phoneFit.bottom, phoneFill.bottom)
        }
    }

    @Test fun fullPanelBottomCornersRenderAndMapToAllTouchCoordinates() {
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString("dual_scaling", "fill").commit()
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitDual(scenario)
            val p = presentation(scenario)!!
            val state = HostProbe.snapshot()
            assertArrayEquals(intArrayOf(0, 0, 1240, 1080), state.sliceArray(12..15))
            lateinit var main: SurfaceView
            scenario.onActivity { main = it.findViewById(R.id.game_surface) }
            waitUntil("system bars remained visible over a full-panel window") {
                var hidden = false
                scenario.onActivity {
                    val first = ViewCompat.getRootWindowInsets(main)
                    val second = ViewCompat.getRootWindowInsets(p.surfaceView)
                    hidden = first != null && second != null &&
                        !first.isVisible(WindowInsetsCompat.Type.systemBars()) &&
                        !second.isVisible(WindowInsetsCompat.Type.systemBars())
                }
                hidden
            }
            assertArrayEquals(intArrayOf(0, 0, main.width, main.height), state.sliceArray(8..11))
            val top = snapshot(main, "thor-fill-top")
            assertEquals(Color.rgb(30, 74, 168), top.getPixel(1, 1))
            assertEquals(Color.rgb(30, 74, 168), top.getPixel(top.width - 2, top.height - 2))
            val bottom = snapshot(p.surfaceView, "thor-fill-bottom")
            for ((x, y) in listOf(1 to 1, 1238 to 1, 1238 to 1078, 1 to 1078))
                assertEquals("full-panel corner was letterboxed", Color.rgb(46, 139, 87), bottom.getPixel(x, y))
            for ((point, mapped) in listOf(
                (1f to 1f) to (0 to 0), (1239f to 1f) to (319 to 0),
                (1239f to 1079f) to (319 to 239), (1f to 1079f) to (0 to 239),
                (620f to 540f) to (160 to 120),
            )) {
                val down = SystemClock.uptimeMillis()
                touch(display!!.display.displayId, MotionEvent.ACTION_DOWN, point.first, point.second, down)
                assertEquals(CtrKeys.TOUCH, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
                assertArrayEquals(intArrayOf(mapped.first, mapped.second), HostProbe.snapshot().sliceArray(4..5))
                touch(display!!.display.displayId, MotionEvent.ACTION_UP, point.first, point.second, down)
                assertEquals(0, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
            }
        }
    }

    @Test fun fillDefaultPreservesExistingPreferencesAndSingleDisplayIntegerScaling() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        prefs.edit().remove("dual_scaling").putBoolean("integer_scaling", true)
            .putString("layout_portrait", AppSettings.PORTRAIT_CONSOLE)
            .putString("layout_landscape", AppSettings.LANDSCAPE_TOP_LARGE)
            .putString("top_display", "second").putString("filter", "nearest")
            .putBoolean("dual_controls", true).commit()
        val settings = AppSettings.load(context)
        assertEquals(DualScaling.FILL, settings.dualScaling)
        assertTrue(settings.integerScaling)
        assertTrue(prefs.getBoolean("integer_scaling", false))
        assertTrue(settings.topOnSecondDisplay)
        assertTrue(settings.dualControls)
        assertFalse(settings.linearFilter)
        assertEquals(AppSettings.PORTRAIT_CONSOLE, settings.portraitLayout)
        assertEquals(AppSettings.LANDSCAPE_TOP_LARGE, settings.landscapeLayout)
        val phone = ScreenLayout.single(1920, 1080, Rect(), settings, 0, false, false)
        assertEquals(Rect(160, 60, 1760, 1020), phone.top)
        assertEquals(Rect(1600, 0, 1920, 240), phone.bottom)
        prefs.edit().putString("dual_scaling", "fit").commit()
        assertEquals(DualScaling.FIT, AppSettings.load(context).dualScaling)
        prefs.edit().putString("dual_scaling", "unknown").commit()
        assertEquals(DualScaling.FILL, AppSettings.load(context).dualScaling)
    }

    @Test fun scalingSettingsExposeBothModesWithoutDisablingPhoneIntegerScaling() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        prefs.edit().remove("dual_scaling").putBoolean("integer_scaling", true).commit()
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                    as SettingsActivity.SettingsFragment
                val scaling = fragment.findPreference<ListPreference>("dual_scaling")!!
                val integer = fragment.findPreference<SwitchPreferenceCompat>("integer_scaling")!!
                assertEquals("fill", scaling.value)
                assertEquals(context.resources.getStringArray(R.array.dual_scaling_entries)[0], scaling.summary)
                assertTrue(integer.isChecked && integer.isEnabled)
                scaling.value = "fit"
                assertEquals(DualScaling.FIT, AppSettings.load(context).dualScaling)
                assertEquals(context.resources.getStringArray(R.array.dual_scaling_entries)[1], scaling.summary)
                fragment.findPreference<SwitchPreferenceCompat>("dual_display")!!.isChecked = false
                assertFalse(scaling.isEnabled)
                assertTrue(integer.isChecked && integer.isEnabled)
            }
        }
    }

    private fun menuAction(label: String) {
        val automation = inst.uiAutomation
        val service = automation.serviceInfo
        val originalFlags = service.flags
        service.flags = originalFlags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
        automation.serviceInfo = service
        try {
            waitUntil("missing pause-menu action: $label") {
                // After touching the second display, its Presentation may
                // remain Android's active window. The pause menu is on main.
                val roots = listOfNotNull(automation.rootInActiveWindow) + automation.windows.mapNotNull { it.root }
                val row = roots.flatMap { it.findAccessibilityNodeInfosByText(label) }
                    .firstOrNull { it.text?.toString() == label } ?: return@waitUntil false
                var target = row
                while (!target.isClickable && target.parent != null) target = target.parent
                target.performAction(AccessibilityNodeInfo.ACTION_CLICK)
            }
            inst.waitForIdleSync()
        } finally {
            service.flags = originalFlags
            automation.serviceInfo = service
        }
    }

    private fun checkFullPanelControls(swapped: Boolean) {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        prefs.edit().putString("dual_scaling", "fill").putBoolean("dual_controls", true)
            .putString("controls_visibility", "auto").putString("top_display", if (swapped) "second" else "main")
            .putBoolean("qol_fast_forward", swapped).commit()
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitUntil("dual window was not ready") { presentation(scenario)?.surfaceWidth == 1240 }
            lateinit var overlay: ControlsOverlayView
            lateinit var main: SurfaceView
            scenario.onActivity {
                overlay = it.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView
                main = it.findViewById(R.id.game_surface)
            }
            waitUntil("dual input was not ready") { var ready = false; scenario.onActivity { ready = overlay.inputEnabled }; ready }
            assertFalse(overlay.controlsVisible)
            val displayId = if (swapped) 0 else display!!.display.displayId
            val x = if (swapped) main.width / 2f else 620f
            val y = if (swapped) main.height / 2f else 540f
            val down = SystemClock.uptimeMillis()
            touch(displayId, MotionEvent.ACTION_DOWN, x, y, down)
            assertArrayEquals(intArrayOf(160, 120), HostProbe.snapshot().sliceArray(4..5))
            assertEquals(CtrKeys.TOUCH, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
            assertFalse("ordinary bottom touch must not reveal or consume controls", overlay.controlsVisible)
            touch(displayId, MotionEvent.ACTION_UP, x, y, down)
            scenario.onActivity { it.onMenuKey() }
            menuAction(context.getString(R.string.menu_controls_show))
            waitUntil("pause menu did not reveal controls") { var visible = false; scenario.onActivity { visible = overlay.controlsVisible && overlay.inputEnabled }; visible }
            assertEquals("auto", prefs.getString("controls_visibility", null))
            scenario.onActivity { it.onMenuKey() }
            menuAction(context.getString(R.string.menu_controls_hide))
            waitUntil("pause menu did not hide controls") { var hidden = false; scenario.onActivity { hidden = !overlay.controlsVisible && overlay.inputEnabled }; hidden }
            assertEquals(0, HostProbe.snapshot()[1])
        }
    }

    @Test fun fullPanelDualAutoControlsCanBeShownAndHiddenFromPauseMenu() = checkFullPanelControls(false)

    @Test fun fullPanelSwappedAutoControlsKeepBottomTouchAndFastForwardMenuSeparate() = checkFullPanelControls(true)

    @Test fun fillFitSwapRotationAndHotplugReleaseTouchesAndKeepWindowGeometry() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        prefs.edit().putString("dual_scaling", "fill").commit()
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitDual(scenario)
            val down = SystemClock.uptimeMillis()
            touch(display!!.display.displayId, MotionEvent.ACTION_DOWN, 930f, 540f, down)
            assertEquals(CtrKeys.TOUCH, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
            scenario.moveToState(Lifecycle.State.CREATED)
            prefs.edit().putString("dual_scaling", "fit").commit()
            scenario.moveToState(Lifecycle.State.RESUMED)
            waitDual(scenario)
            assertEquals(0, HostProbe.snapshot()[1])
            assertArrayEquals(intArrayOf(0, 75, 1240, 930), HostProbe.snapshot().sliceArray(12..15))

            scenario.moveToState(Lifecycle.State.CREATED)
            prefs.edit().putString("dual_scaling", "fill").putString("top_display", "second").commit()
            scenario.moveToState(Lifecycle.State.RESUMED)
            waitUntil("full-panel assignment did not swap") {
                presentation(scenario)?.surfaceWidth == 1240 && HostProbe.snapshot()[6] == NativeBridge.WINDOW_SECOND
            }
            lateinit var main: SurfaceView
            scenario.onActivity { main = it.findViewById(R.id.game_surface) }
            val initialWidth = main.width
            assertArrayEquals(intArrayOf(0, 0, main.width, main.height), HostProbe.snapshot().sliceArray(12..15))
            val rendered = HostProbe.snapshot()[16]
            waitUntil("swapped main surface did not draw") { HostProbe.snapshot()[16] > rendered + 1 }
            val bottom = snapshot(main, "thor-fill-bottom-on-main")
            assertEquals(Color.rgb(46, 139, 87), bottom.getPixel(1, 1))
            assertEquals(Color.rgb(46, 139, 87), bottom.getPixel(bottom.width - 2, bottom.height - 2))
            val x = main.width * 0.75f
            val y = main.height * 0.25f
            val mainDown = SystemClock.uptimeMillis()
            touch(0, MotionEvent.ACTION_DOWN, x, y, mainDown)
            assertArrayEquals(intArrayOf(240, 60), HostProbe.snapshot().sliceArray(4..5))
            assertEquals(CtrKeys.TOUCH, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
            scenario.onActivity {
                it.requestedOrientation = if (main.width > main.height)
                    ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            waitUntil("main display did not rotate") { main.width != initialWidth }
            waitUntil("rotation retained a touch from old coordinates") { HostProbe.snapshot()[1] and CtrKeys.TOUCH == 0 }
            assertArrayEquals(intArrayOf(0, 0, main.width, main.height), HostProbe.snapshot().sliceArray(12..15))
            touch(0, MotionEvent.ACTION_UP, x, y, mainDown)

            inst.runOnMainSync { display!!.resize(1000, 1000, 240) }
            waitUntil("resized full-panel display was not recreated") {
                presentation(scenario)?.surfaceWidth == 1000 && presentation(scenario)?.surfaceHeight == 1000
            }
            assertArrayEquals(intArrayOf(0, 0, 1000, 1000), HostProbe.snapshot().sliceArray(8..11))
            removeDisplay()
            waitUntil("full-panel display loss did not fall back") { presentation(scenario) == null && HostProbe.snapshot()[6] == 0 }
            assertEquals(0, HostProbe.snapshot()[1])
            assertTrue(HostProbe.snapshot()[15] < main.height)
            addDisplay()
            waitUntil("full-panel display did not reattach") {
                presentation(scenario)?.surfaceWidth == 1240 && HostProbe.snapshot()[6] == NativeBridge.WINDOW_SECOND
            }
            assertArrayEquals(intArrayOf(0, 0, 1240, 1080), HostProbe.snapshot().sliceArray(8..11))
            assertArrayEquals(intArrayOf(0, 0, main.width, main.height), HostProbe.snapshot().sliceArray(12..15))
        }
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
            // Count from after input arrived; frames drawn while Android was
            // injecting the event say nothing about its visible response.
            val touchFrame = HostProbe.snapshot()[17]
            waitUntil("touch frame was not drawn") { HostProbe.snapshot()[17] > touchFrame + 1 }
            val deadline = SystemClock.uptimeMillis() + 2000
            var pixel: Int
            do {
                assertTrue("touch released before visual response", HostProbe.snapshot()[1] and CtrKeys.TOUCH != 0)
                pixel = snapshot(p.surfaceView, "thor-bottom-touch").getPixel(930, 540)
                if (pixel == Color.rgb(255, 48, 48)) break
                SystemClock.sleep(30)
            } while (SystemClock.uptimeMillis() < deadline)
            assertEquals(Color.rgb(255, 48, 48), pixel)
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

    @Test fun topLargeLayoutKeepsBothScreensAtWholePixelScales() {
        val settings = AppSettings.load(context).copy(
            landscapeLayout = AppSettings.LANDSCAPE_TOP_LARGE,
            integerScaling = true,
        )
        fun layout(width: Int, height: Int, safe: Rect, integer: Boolean = true) =
            ScreenLayout.single(width, height, safe, settings.copy(integerScaling = integer), 0, false, false)

        val fullHd = layout(1920, 1080, Rect())
        assertEquals(Rect(160, 60, 1760, 1020), fullHd.top)
        assertEquals(Rect(1600, 0, 1920, 240), fullHd.bottom)

        val inset = layout(2560, 1440, Rect(37, 11, 29, 19))
        assertEquals(Rect(284, 116, 2284, 1316), inset.top)
        assertEquals(Rect(1891, 11, 2531, 491), inset.bottom)

        // Fractional fill is unchanged when disabled; windows too small for
        // 1x must still downscale so the touch screen remains within bounds.
        assertEquals(Rect(1315, 0, 1920, 454), layout(1920, 1080, Rect(), false).bottom)
        val tiny = layout(360, 180, Rect()).bottom!!
        assertEquals(Rect(259, 0, 360, 76), tiny)
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

    @Test fun keyboardAliasesAndControllerRemovalReleaseOnlyTheirOwnInputs() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitUntil("game not started") { NativeBridge.isStarted() }
            fun key(code: Int, action: Int, device: Int = 101) {
                val t = SystemClock.uptimeMillis()
                scenario.onActivity { it.dispatchKeyEvent(KeyEvent(t, t, action, code, 0, 0, device, 0)) }
            }
            key(KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.ACTION_DOWN)
            key(KeyEvent.KEYCODE_SHIFT_RIGHT, KeyEvent.ACTION_DOWN)
            key(KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.ACTION_UP)
            assertEquals(CtrKeys.SELECT, HostProbe.snapshot()[1])
            key(KeyEvent.KEYCODE_SHIFT_RIGHT, KeyEvent.ACTION_UP)
            assertEquals(0, HostProbe.snapshot()[1])
            key(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.ACTION_DOWN, 101)
            key(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.ACTION_DOWN, 202)
            scenario.onActivity { it.onInputDeviceRemoved(101) }
            assertEquals(CtrKeys.B, HostProbe.snapshot()[1])
            key(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.ACTION_UP, 202)
            assertEquals(0, HostProbe.snapshot()[1])

            val t = SystemClock.uptimeMillis()
            val coords = MotionEvent.PointerCoords().apply { x = 1f; y = 0f }
            val event = MotionEvent.obtain(t, t, MotionEvent.ACTION_MOVE, 1,
                arrayOf(MotionEvent.PointerProperties().apply { id = 0 }), arrayOf(coords),
                0, 0, 1f, 1f, 303, 0, InputDevice.SOURCE_JOYSTICK, 0)
            scenario.onActivity { it.dispatchGenericMotionEvent(event) }
            event.recycle()
            assertTrue(HostProbe.snapshot()[2] > 0)
            scenario.onActivity { it.onInputDeviceRemoved(303) }
            assertEquals(0, HostProbe.snapshot()[2])
            assertEquals(0, HostProbe.snapshot()[1])
        }
    }

    @Test fun secondaryMenuBackAndActivityRecreationKeepBothWindowsWorking() {
        PreferenceManager.getDefaultSharedPreferences(context).edit().putBoolean("keep_screen_on", false).commit()
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitDual(scenario)
            val p = presentation(scenario)!!
            scenario.onActivity {
                assertEquals(0, p.window!!.attributes.flags and WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                p.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_MODE))
            }
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            scenario.onActivity {
                p.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK))
                p.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK))
            }
            waitUntil("secondary-focused Back did not close menu") { HostProbe.snapshot()[0] == NativeBridge.STATE_RUNNING }
            scenario.recreate()
            waitDual(scenario)
            val before = HostProbe.snapshot()
            waitUntil("recreated surfaces did not draw") {
                val now = HostProbe.snapshot()
                now[16] > before[16] && now[17] > before[17]
            }
            inst.runOnMainSync { display!!.resize(1000, 1000, 240) }
            waitUntil("changed display metrics did not recreate bottom window") {
                presentation(scenario)?.surfaceWidth == 1000 && presentation(scenario)?.surfaceHeight == 1000
            }
        }
    }

    @Test fun secondaryMoveStreamIsDeliveredWhileRenderingAndWhilePaused() {
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitDual(scenario)
            val p = presentation(scenario)!!
            val displayId = display!!.display.displayId
            for (paused in listOf(false, true)) {
                if (paused) scenario.onActivity {
                    p.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_MODE))
                }
                val down = SystemClock.uptimeMillis()
                touch(displayId, MotionEvent.ACTION_DOWN, 310f, 307.5f, down)
                for (step in 1..10) {
                    // Goes through InputDispatcher, not View.dispatchTouchEvent.
                    // A batched secondary MOVE used to sit unacknowledged for
                    // five seconds on API 30 and produce an application ANR.
                    touch(displayId, MotionEvent.ACTION_MOVE, 310f + 62f * step, 307.5f + 23.25f * step, down)
                }
                if (paused) {
                    assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                    assertEquals(0, HostProbe.snapshot()[1])
                } else {
                    assertTrue(HostProbe.snapshot()[1] and CtrKeys.TOUCH != 0)
                    assertArrayEquals(intArrayOf(240, 120), HostProbe.snapshot().sliceArray(4..5))
                }
                touch(displayId, MotionEvent.ACTION_UP, 930f, 540f, down)
                assertTrue("secondary drag stalled", SystemClock.uptimeMillis() - down < 4000)
                assertEquals(0, HostProbe.snapshot()[1] and CtrKeys.TOUCH)
            }
        }
    }
}
