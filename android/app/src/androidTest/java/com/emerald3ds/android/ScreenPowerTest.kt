package com.emerald3ds.android

import android.app.Application
import android.app.KeyguardManager
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import android.view.Display
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.FrameLayout
import androidx.appcompat.app.AlertDialog
import androidx.lifecycle.Lifecycle
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class ScreenPowerTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val context get() = inst.targetContext
    private val awake = ScreenPowerState(interactive = true, keyguardLocked = false, displayState = Display.STATE_ON)
    private val dark get() = awake.copy(displayState = Display.STATE_OFF)
    private var display: VirtualDisplay? = null
    private var reader: ImageReader? = null

    @Before fun prepare() {
        check(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(context).edit().clear()
            .putBoolean("dual_display", false).putBoolean("qol_fast_forward", true)
            .putString("controls_visibility", "always").commit()
        inst.runOnMainSync { InputHub.clear() }
    }

    @After fun cleanup() {
        inst.runOnMainSync {
            display?.release(); display = null
            reader?.close(); reader = null
            InputHub.clear()
        }
    }

    private fun waitUntil(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < deadline) {
            if (condition()) return
            SystemClock.sleep(30)
        }
        fail(message)
    }

    private fun overlay(activity: GameActivity) =
        activity.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView

    private fun ready(scenario: ActivityScenario<GameActivity>) {
        waitUntil("native game/input did not resume") {
            var accepts = false
            scenario.onActivity { accepts = overlay(it).inputEnabled }
            accepts && HostProbe.snapshot()[0] == NativeBridge.STATE_RUNNING
        }
    }

    private class Power(var state: ScreenPowerState)

    private fun installPower(scenario: ActivityScenario<GameActivity>): Power {
        val power = Power(awake)
        scenario.onActivity {
            it.screenPowerSnapshot = { power.state }
            it.refreshScreenPower()
        }
        return power
    }

    private fun power(scenario: ActivityScenario<GameActivity>, power: Power, state: ScreenPowerState) {
        scenario.onActivity {
            power.state = state
            it.refreshScreenPower()
        }
    }

    private fun paused(scenario: ActivityScenario<GameActivity>, checkFrames: Boolean = false) {
        assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
        scenario.onActivity {
            assertFalse(overlay(it).inputEnabled)
            it.presentation?.let { second -> assertFalse(second.touchView.inputEnabled) }
        }
        val input = HostProbe.snapshot()
        assertEquals("held game buttons survived suspension", 0, input[1])
        assertEquals(0, input[2]); assertEquals(0, input[3])
        if (checkFrames) {
            assertTrue("game thread did not acknowledge pause", NativeBridge.awaitPaused(5000))
            val before = HostProbe.snapshot()
            SystemClock.sleep(200)
            val after = HostProbe.snapshot()
            assertEquals("main frames advanced while suspended", before[16], after[16])
            assertEquals("secondary frames advanced while suspended", before[17], after[17])
        }
    }

    private fun key(code: Int, device: Int = 971): KeyEvent {
        SystemClock.sleep(2)
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0, 0, device, 0,
            KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_GAMEPAD)
    }

    private fun up(press: KeyEvent) = KeyEvent(press.downTime, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP,
        press.keyCode, 0, 0, press.deviceId, 0, KeyEvent.FLAG_FROM_SYSTEM, press.source)

    private fun motion(x: Float = 0f, r2: Float = 0f, use: (MotionEvent) -> Unit) {
        val now = SystemClock.uptimeMillis()
        val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
            arrayOf(MotionEvent.PointerCoords().apply {
                setAxisValue(MotionEvent.AXIS_X, x); setAxisValue(MotionEvent.AXIS_RTRIGGER, r2)
            }), 0, 0, 1f, 1f, 971, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try { use(event) } finally { event.recycle() }
    }

    private fun touch(view: ControlsOverlayView) {
        val rect = view.bottomScreenBounds()!!
        val now = SystemClock.uptimeMillis()
        val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0; toolType = MotionEvent.TOOL_TYPE_FINGER }),
            arrayOf(MotionEvent.PointerCoords().apply {
                x = rect.exactCenterX(); y = rect.exactCenterY(); pressure = 1f; size = 1f
            }), 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
        try { assertTrue(view.onTouchEvent(event)) } finally { event.recycle() }
    }

    private fun dialog(activity: GameActivity, name: String = "menuDialog"): AlertDialog? =
        GameActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.get(activity) as? AlertDialog

    private fun fastState(activity: GameActivity) = GameActivity::class.java.getDeclaredField("fastForward")
        .apply { isAccessible = true }.get(activity) as FastForwardState

    private fun openMenu(scenario: ActivityScenario<GameActivity>) {
        scenario.onActivity { it.dispatchKeyEvent(key(KeyEvent.KEYCODE_MENU)) }
        assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
    }

    private fun resumeMenu(scenario: ActivityScenario<GameActivity>) {
        scenario.onActivity {
            val menu = dialog(it)!!
            assertTrue(menu.listView.performItemClick(null, 0, menu.listView.adapter.getItemId(0)))
        }
        inst.waitForIdleSync()
    }

    @Test fun nonUsablePowerPausesWhileActivityRemainsResumedAndClearsInput() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            val blocked = listOf(Display.STATE_OFF, Display.STATE_DOZE, Display.STATE_DOZE_SUSPEND,
                Display.STATE_ON_SUSPEND, Display.STATE_UNKNOWN).map { awake.copy(displayState = it) } +
                awake.copy(interactive = false)
            for (snapshot in blocked) {
                val a = key(KeyEvent.KEYCODE_BUTTON_B)
                val l2 = key(KeyEvent.KEYCODE_BUTTON_L2)
                scenario.onActivity {
                    it.dispatchKeyEvent(a); it.dispatchKeyEvent(l2)
                    motion(x = 1f) { event -> it.dispatchGenericMotionEvent(event) }
                    touch(overlay(it))
                    assertEquals(4, fastState(it).speed)
                }
                assertTrue(HostProbe.snapshot()[1] and CtrKeys.TOUCH != 0)
                power(scenario, state, snapshot)
                assertEquals("test must not rely on onPause", Lifecycle.State.RESUMED, scenario.state)
                paused(scenario, checkFrames = true)
                scenario.onActivity { assertEquals("L2 hold survived suspension", 1, fastState(it).speed) }
                power(scenario, state, awake)
                ready(scenario)
                scenario.onActivity { it.dispatchKeyEvent(up(a)); it.dispatchKeyEvent(up(l2)) }
            }
        }
    }

    @Test fun wakingWhileKeyguardIsShowingDoesNotResumeUntilUnlockedAndOn() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            power(scenario, state, dark.copy(interactive = false, keyguardLocked = true))
            paused(scenario)
            power(scenario, state, awake.copy(keyguardLocked = true))
            paused(scenario, checkFrames = true)
            scenario.onActivity {
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_B))
                touch(overlay(it))
            }
            assertEquals(0, HostProbe.snapshot()[1])
            power(scenario, state, dark)
            paused(scenario)
            power(scenario, state, awake)
            ready(scenario)
        }
    }

    @Test fun reopeningPreservesPauseMenuAndDismissingWhileDarkDoesNotResume() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            openMenu(scenario)
            power(scenario, state, dark)
            power(scenario, state, awake)
            paused(scenario)
            scenario.onActivity { assertTrue(dialog(it)?.isShowing == true) }
            resumeMenu(scenario)
            ready(scenario)
            openMenu(scenario)
            power(scenario, state, dark)
            resumeMenu(scenario)
            paused(scenario, checkFrames = true)
            power(scenario, state, awake)
            ready(scenario)
        }
    }

    @Test fun menuDismissalSamplesNewlyDarkTelemetryBeforeResuming() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            openMenu(scenario)
            inst.waitForIdleSync()
            scenario.onActivity {
                val menu = dialog(it)!!
                // The OS changed before its broadcast/display callback was
                // dispatched. Do not explicitly refresh the Activity here.
                state.state = dark
                assertTrue(menu.listView.performItemClick(null, 0, menu.listView.adapter.getItemId(0)))
            }
            inst.waitForIdleSync()
            scenario.onActivity { assertNull("pause menu did not dismiss", dialog(it)) }
            assertEquals(Lifecycle.State.RESUMED, scenario.state)
            paused(scenario, checkFrames = true)
            power(scenario, state, awake)
            ready(scenario)
        }
    }

    @Test fun exportHoldAndScreenPowerMustBothClearBeforeResume() {
        val finish = CountDownLatch(1)
        val started = CountDownLatch(1)
        val model = GameFilesModel(context.applicationContext as Application)
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            try {
                scenario.onActivity {
                    model.submit(GameFilesModel.Action.EXPORT_SAVE) {
                        check(NativeBridge.awaitPaused(5000))
                        started.countDown()
                        check(finish.await(20, TimeUnit.SECONDS))
                        false
                    }
                }
                assertTrue(started.await(5, TimeUnit.SECONDS))
                power(scenario, state, dark)
                power(scenario, state, awake)
                paused(scenario)
                power(scenario, state, dark)
                finish.countDown()
                waitUntil("export hold did not finish") {
                    var done = false
                    inst.runOnMainSync { done = !GameFilesModel.exportPending }
                    done
                }
                paused(scenario, checkFrames = true)
                power(scenario, state, awake)
                ready(scenario)
            } finally {
                finish.countDown()
                waitUntil("export hold leaked after test") {
                    var done = false
                    inst.runOnMainSync { done = !GameFilesModel.exportPending }
                    done
                }
            }
        }
    }

    @Test fun exportCompletionSamplesNewlyDarkTelemetryBeforeResuming() {
        val finish = CountDownLatch(1)
        val started = CountDownLatch(1)
        val model = GameFilesModel(context.applicationContext as Application)
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            try {
                scenario.onActivity {
                    model.submit(GameFilesModel.Action.EXPORT_SAVE) {
                        check(NativeBridge.awaitPaused(5000))
                        started.countDown()
                        check(finish.await(20, TimeUnit.SECONDS))
                        false
                    }
                }
                assertTrue(started.await(5, TimeUnit.SECONDS))
                inst.waitForIdleSync()
                scenario.onActivity {
                    // Complete the worker after changing only the reader's
                    // telemetry. Its pause-hold callback must sample power.
                    state.state = dark
                    finish.countDown()
                }
                waitUntil("export hold did not finish") {
                    var done = false
                    inst.runOnMainSync { done = !GameFilesModel.exportPending }
                    done
                }
                assertEquals(Lifecycle.State.RESUMED, scenario.state)
                paused(scenario, checkFrames = true)
                power(scenario, state, awake)
                ready(scenario)
            } finally {
                finish.countDown()
                waitUntil("export hold leaked after test") {
                    var done = false
                    inst.runOnMainSync { done = !GameFilesModel.exportPending }
                    done
                }
            }
        }
    }

    @Test fun screenPowerLossCancelsPendingShinyConfirmationToStay() {
        val worker = Executors.newSingleThreadExecutor()
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val state = installPower(scenario)
                val result = worker.submit<Boolean> { NativeBridge.testShinyFleeRoundTrip() }
                waitUntil("shiny prompt did not appear") {
                    var shown = false
                    scenario.onActivity { shown = dialog(it, "shinyDialog")?.isShowing == true }
                    shown
                }
                power(scenario, state, dark)
                assertFalse("power loss approved fleeing", result.get(5, TimeUnit.SECONDS))
                paused(scenario)
                scenario.onActivity { assertNull(dialog(it, "shinyDialog")) }
                power(scenario, state, awake)
                ready(scenario)
                val next = worker.submit<Boolean> { NativeBridge.testShinyFleeRoundTrip() }
                waitUntil("canceled confirmation prevented a later request") {
                    var shown = false
                    scenario.onActivity { shown = dialog(it, "shinyDialog")?.isShowing == true }
                    shown
                }
                scenario.onActivity { dialog(it, "shinyDialog")!!.getButton(AlertDialog.BUTTON_NEGATIVE).performClick() }
                assertFalse(next.get(5, TimeUnit.SECONDS))
                ready(scenario)
            }
        } finally {
            worker.shutdownNow()
        }
    }

    @Test fun queuedShinyRunSamplesDarkTelemetryAndStaysInstead() {
        val worker = Executors.newSingleThreadExecutor()
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val state = installPower(scenario)
                val result = worker.submit<Boolean> { NativeBridge.testShinyFleeRoundTrip() }
                waitUntil("shiny prompt did not appear") {
                    var shown = false
                    scenario.onActivity { shown = dialog(it, "shinyDialog")?.isShowing == true }
                    shown
                }
                scenario.onActivity {
                    val prompt = dialog(it, "shinyDialog")!!
                    state.state = dark // The display notification is still queued.
                    prompt.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                }
                assertFalse("queued Run approved fleeing with the display off", result.get(5, TimeUnit.SECONDS))
                paused(scenario, checkFrames = true)
                power(scenario, state, awake)
                ready(scenario)
            }
        } finally {
            worker.shutdownNow()
        }
    }

    @Test fun powerPausePreservesReleaseOnlyShoulderAndTriggerGuards() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            val left = key(KeyEvent.KEYCODE_BUTTON_L1)
            val r2 = key(KeyEvent.KEYCODE_BUTTON_R2)
            scenario.onActivity {
                it.dispatchKeyEvent(left); it.dispatchKeyEvent(r2)
                motion(r2 = 1f) { event -> it.dispatchGenericMotionEvent(event) }
                assertTrue(fastState(it).toggled)
            }
            power(scenario, state, dark)
            paused(scenario)
            val right = key(KeyEvent.KEYCODE_BUTTON_R1)
            scenario.onActivity { it.dispatchKeyEvent(right) }
            power(scenario, state, awake)
            ready(scenario)
            scenario.onActivity {
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_L1))
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_R1))
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_R2))
                motion(r2 = 1f) { event -> it.dispatchGenericMotionEvent(event) }
                assertNull("held shoulders reopened pause", dialog(it))
                assertTrue("held R2 toggled after power resume", fastState(it).toggled)
                it.dispatchKeyEvent(up(left)); it.dispatchKeyEvent(up(right)); it.dispatchKeyEvent(up(r2))
                motion(r2 = 0f) { event -> it.dispatchGenericMotionEvent(event) }
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_L1))
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_R1))
            }
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            resumeMenu(scenario)
            ready(scenario)
            scenario.onActivity {
                it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_R2))
                motion(r2 = 1f) { event -> it.dispatchGenericMotionEvent(event) }
                assertFalse("real release failed to rearm R2", fastState(it).toggled)
            }
        }
    }

    private fun addDisplay() {
        inst.runOnMainSync {
            if (reader == null) reader = ImageReader.newInstance(1240, 1080, PixelFormat.RGBA_8888, 3).apply {
                setOnImageAvailableListener({ it.acquireLatestImage()?.close() }, Handler(Looper.getMainLooper()))
            }
            display = context.getSystemService(DisplayManager::class.java).createVirtualDisplay(
                "Thor screen power test", 1240, 1080, 240, reader!!.surface,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC or DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION or
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY)
            assertNotNull(display)
        }
    }

    private fun dual(scenario: ActivityScenario<GameActivity>) {
        waitUntil("dual display was not restored") {
            var second = false
            scenario.onActivity { second = it.presentation?.surfaceWidth == 1240 }
            second && HostProbe.snapshot()[7] == NativeBridge.WINDOW_SECOND
        }
        val frame = HostProbe.snapshot()[17]
        waitUntil("secondary display did not draw") { HostProbe.snapshot()[17] > frame + 1 }
    }

    @Test fun secondaryPowerOffFallsBackWithoutPausingOrDefeatingMainPowerHold() {
        PreferenceManager.getDefaultSharedPreferences(context).edit().putBoolean("dual_display", true).commit()
        addDisplay()
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val state = installPower(scenario)
            dual(scenario)
            scenario.onActivity { touch(it.presentation!!.touchView) }
            assertTrue(HostProbe.snapshot()[1] and CtrKeys.TOUCH != 0)
            inst.runOnMainSync { display!!.surface = null }
            waitUntil("secondary OFF did not fall back") {
                var gone = false
                scenario.onActivity { gone = it.presentation == null }
                gone && HostProbe.snapshot()[7] == NativeBridge.WINDOW_MAIN
            }
            assertEquals(Display.STATE_OFF, display!!.display.state)
            assertEquals(NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
            assertEquals(0, HostProbe.snapshot()[1])
            inst.runOnMainSync { display!!.surface = reader!!.surface }
            dual(scenario)
            power(scenario, state, dark)
            inst.runOnMainSync { display!!.release(); display = null }
            waitUntil("removed secondary display remained selected") { HostProbe.snapshot()[7] == NativeBridge.WINDOW_MAIN }
            paused(scenario, checkFrames = true)
            power(scenario, state, awake)
            ready(scenario)
            addDisplay()
            dual(scenario)
        }
    }

    private fun shell(command: String) {
        ParcelFileDescriptor.AutoCloseInputStream(inst.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
    }

    @Test fun realSystemSleepWakeUsesPlatformPowerSignalsAndStopsNativeFrames() {
        val powerManager = context.getSystemService(PowerManager::class.java)
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        val scenario = ActivityScenario.launch(GameActivity::class.java)
        try {
            // This test intentionally retains the production snapshot reader.
            ready(scenario)
            scenario.onActivity { it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_B)) }
            assertTrue(HostProbe.snapshot()[1] != 0)
            shell("input keyevent 223") // KEYCODE_SLEEP
            waitUntil("system did not enter sleep") { !powerManager.isInteractive }
            waitUntil("system sleep did not pause native game") { HostProbe.snapshot()[0] == NativeBridge.STATE_PAUSED }
            assertEquals(0, HostProbe.snapshot()[1])
            assertTrue(NativeBridge.awaitPaused(5000))
            val stopped = HostProbe.snapshot()
            SystemClock.sleep(250)
            assertEquals(stopped[16], HostProbe.snapshot()[16])
            assertEquals(stopped[17], HostProbe.snapshot()[17])
            shell("input keyevent 224") // KEYCODE_WAKEUP
            waitUntil("system did not wake") { powerManager.isInteractive }
            val locked = keyguard.isKeyguardLocked
            Log.i("ScreenPowerTest", "real wake: keyguard=$locked native=${HostProbe.snapshot()[0]}")
            if (locked) assertEquals("game resumed behind keyguard", NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            shell("wm dismiss-keyguard")
            ready(scenario)
            val frame = HostProbe.snapshot()[16]
            waitUntil("main frames did not restart after wake") { HostProbe.snapshot()[16] > frame + 1 }
            assertEquals(0, HostProbe.snapshot()[1])
        } finally {
            try {
                shell("input keyevent 224")
                shell("wm dismiss-keyguard")
            } finally {
                scenario.close()
            }
        }
    }
}
