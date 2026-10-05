package com.emerald3ds.android

import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.FrameLayout
import androidx.appcompat.app.AlertDialog
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Uses only pre-diagnostics app APIs, so this regression can also run on the baseline APK. */
@RunWith(AndroidJUnit4::class)
class SettingsDialogInputTest {
    private val inst = InstrumentationRegistry.getInstrumentation()

    @Before fun prepare() {
        PreferenceManager.getDefaultSharedPreferences(inst.targetContext).edit().clear()
            .putBoolean("dual_display", false).putBoolean("qol_fast_forward", true).commit()
        inst.runOnMainSync { InputHub.clear() }
    }

    private fun key(code: Int): KeyEvent {
        SystemClock.sleep(2)
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0, 0, 993, 0, KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_GAMEPAD)
    }

    private fun up(press: KeyEvent) = KeyEvent(press.downTime, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP,
        press.keyCode, 0, 0, press.deviceId, 0, KeyEvent.FLAG_FROM_SYSTEM, press.source)

    private fun trigger(value: Float, dispatch: (MotionEvent) -> Unit) {
        val now = SystemClock.uptimeMillis()
        val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
            arrayOf(MotionEvent.PointerCoords().apply { setAxisValue(MotionEvent.AXIS_RTRIGGER, value) }),
            0, 0, 1f, 1f, 993, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try { dispatch(event) } finally { event.recycle() }
    }

    private fun ready(scenario: ActivityScenario<GameActivity>) {
        val deadline = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < deadline) {
            var enabled = false
            scenario.onActivity { enabled = (it.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView).inputEnabled }
            if (enabled && HostProbe.snapshot()[0] == NativeBridge.STATE_RUNNING) return
            SystemClock.sleep(30)
        }
        fail("game did not resume")
    }

    private fun menu(activity: GameActivity) = GameActivity::class.java.getDeclaredField("menuDialog")
        .apply { isAccessible = true }.get(activity) as AlertDialog

    private fun fast(activity: GameActivity) = GameActivity::class.java.getDeclaredField("fastForward")
        .apply { isAccessible = true }.get(activity) as FastForwardState

    @Test fun aboutWindowReleasesRearmHeldGuideAndMixedR2ExactlyOnce() {
        val monitor = inst.addMonitor(SettingsActivity::class.java.name, null, false)
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val r2 = key(KeyEvent.KEYCODE_BUTTON_R2)
                val guide = key(KeyEvent.KEYCODE_BUTTON_MODE)
                scenario.onActivity {
                    it.dispatchKeyEvent(r2)
                    trigger(1f) { event -> it.dispatchGenericMotionEvent(event) }
                    assertTrue(fast(it).toggled)
                    it.dispatchKeyEvent(guide)
                    val pause = menu(it)
                    pause.listView.performItemClick(null, 1, pause.listView.adapter.getItemId(1))
                }
                val settings = inst.waitForMonitorWithTimeout(monitor, 10_000) as? SettingsActivity
                assertNotNull(settings)
                inst.waitForIdleSync()
                inst.runOnMainSync {
                    val fragment = settings!!.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
                    fragment.javaClass.getDeclaredMethod("showAbout").apply { isAccessible = true }.invoke(fragment)
                    val field = try { fragment.javaClass.getDeclaredField("aboutDialog") }
                        catch (_: NoSuchFieldException) { fragment.javaClass.getDeclaredField("dialog") }
                    val about = field.apply { isAccessible = true }.get(fragment) as AlertDialog
                    val callback = about.window!!.callback
                    callback.dispatchKeyEvent(up(guide))
                    callback.dispatchKeyEvent(up(r2))
                    trigger(0f) { callback.dispatchGenericMotionEvent(it) }
                    assertEquals(0, HostProbe.snapshot()[1])
                    about.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                    settings.finish()
                }
                ready(scenario)
                val next = key(KeyEvent.KEYCODE_BUTTON_R2)
                scenario.onActivity {
                    it.dispatchKeyEvent(next)
                    trigger(1f) { event -> it.dispatchGenericMotionEvent(event) }
                    assertFalse("About consumed R2 release instead of rearming it", fast(it).toggled)
                    it.dispatchKeyEvent(up(next))
                    trigger(0f) { event -> it.dispatchGenericMotionEvent(event) }
                    it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_MODE))
                }
                assertEquals("About consumed Guide release instead of rearming it", NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                scenario.onActivity {
                    val pause = menu(it)
                    pause.listView.performItemClick(null, 0, pause.listView.adapter.getItemId(0))
                }
                ready(scenario)
                scenario.onActivity { it.dispatchKeyEvent(key(KeyEvent.KEYCODE_BUTTON_MODE)) }
                assertEquals("held Guide repeat reopened the menu", NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
            }
        } finally {
            inst.removeMonitor(monitor)
        }
    }
}
