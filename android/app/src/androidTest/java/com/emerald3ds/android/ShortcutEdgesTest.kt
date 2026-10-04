package com.emerald3ds.android

import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
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

/** Normalized hardware repeats must not become additional app actions. */
@RunWith(AndroidJUnit4::class)
class ShortcutEdgesTest {
    private val inst = InstrumentationRegistry.getInstrumentation()

    @Before fun resetInput() {
        check(BuildConfig.HOST_HARNESS)
        inst.runOnMainSync { InputHub.clear() }
    }

    private class Owner : PhysicalInput.Callbacks {
        val input = PhysicalInput(this, rightTriggerAxis = { false })
        var menus = 0
        var screens = 0
        override fun onPhysicalInput() {}
        override fun onMenuKey() { menus++; input.clear() }
        override fun onToggleBottomScreen() { screens++ }
        val actions get() = menus + screens
    }

    private fun down(code: Int, device: Int = 42, repeat: Int = 0): KeyEvent {
        SystemClock.sleep(2)
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, repeat, 0, device, 0,
            KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_GAMEPAD)
    }

    private fun up(press: KeyEvent, canceled: Boolean = false) =
        KeyEvent(press.downTime, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP, press.keyCode,
            0, 0, press.deviceId, 0, KeyEvent.FLAG_FROM_SYSTEM or
                (if (canceled) KeyEvent.FLAG_CANCELED else 0), press.source)

    @Test fun normalizedHeldShortcutsFireOnceUntilTheirRealRelease() {
        inst.runOnMainSync {
            for (code in listOf(KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR,
                KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_MENU, KeyEvent.KEYCODE_ESCAPE)) {
                val owner = Owner()
                val input = owner.input
                val held = down(code)
                assertTrue(input.onKey(held))
                assertEquals(1, owner.actions)
                val other = down(KeyEvent.KEYCODE_BUTTON_A)
                assertTrue(input.onKey(other)); assertTrue(input.onKey(up(other)))
                // The actual uinput trace demonstrated that a held EV_KEY2
                // can arrive with count0 and entirely new equal timestamps.
                repeat(3) { assertTrue(input.onKey(down(code))) }
                assertEquals("held key $code repeated its app action", 1, owner.actions)
                assertTrue(input.onKey(up(held)))
                val fresh = down(code)
                assertTrue(input.onKey(fresh))
                assertEquals(2, owner.actions)
                assertTrue(input.onKey(up(fresh)))
                assertEquals(0, InputHub.sentKeys)
            }
        }
    }

    @Test fun pauseAndCanceledUpsKeepShortcutHeldUntilObservedRelease() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val menu = down(KeyEvent.KEYCODE_BUTTON_MODE)
            assertTrue(input.onKey(menu))
            input.observeKeyEvent(up(menu, canceled = true), suspended = true)
            input.onInputResumed()
            assertTrue(input.onKey(down(menu.keyCode)))
            assertEquals("focus cancellation rearmed the menu key", 1, owner.menus)
            input.observeKeyEvent(up(menu), suspended = true)
            assertTrue(input.onKey(down(menu.keyCode)))
            assertEquals(2, owner.menus)

            val hidden = down(KeyEvent.KEYCODE_BUTTON_THUMBL)
            input.observeKeyEvent(hidden, suspended = true)
            input.onInputResumed()
            assertTrue(input.onKey(down(hidden.keyCode)))
            assertEquals("a shortcut held in another app window became a new action", 0, owner.screens)
            input.observeKeyEvent(up(hidden), suspended = true)
            assertTrue(input.onKey(down(hidden.keyCode)))
            assertEquals(1, owner.screens)
            input.clear()
        }
    }

    @Test fun eachControllerOwnsItsShortcutAndRemovalRearmsOnlyThatController() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val code = KeyEvent.KEYCODE_BUTTON_THUMBR
            val first = down(code, device = 11)
            val second = down(code, device = 22)
            assertTrue(input.onKey(first)); assertTrue(input.onKey(second))
            assertEquals(2, owner.screens)
            assertTrue(input.onKey(down(code, device = 11)))
            assertEquals(2, owner.screens)
            input.removeDevice(11)
            assertTrue(input.onKey(down(code, device = 11)))
            assertEquals(3, owner.screens)
            assertTrue(input.onKey(down(code, device = 22)))
            assertEquals(3, owner.screens)
            input.observeKeyEvent(up(second), suspended = true)
            assertTrue(input.onKey(down(code, device = 22)))
            assertEquals(4, owner.screens)
            input.clear()
        }
    }

    @Test fun firstSeenRepeatCannotBecomeFreshWhenAndroidResetsItsCount() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val repeated = down(KeyEvent.KEYCODE_BUTTON_THUMBL, repeat = 2)
            assertTrue(input.onKey(repeated))
            assertEquals(0, owner.screens)
            assertTrue(input.onKey(down(repeated.keyCode)))
            assertEquals(0, owner.screens)
            assertTrue(input.onKey(up(repeated)))
            assertTrue(input.onKey(down(repeated.keyCode)))
            assertEquals(1, owner.screens)
            input.clear()
        }
    }

    private fun ready(scenario: ActivityScenario<GameActivity>) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            var accepts = false
            scenario.onActivity {
                accepts = (it.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView).inputEnabled
            }
            if (accepts && HostProbe.snapshot()[0] == NativeBridge.STATE_RUNNING) return
            SystemClock.sleep(30)
        }
        fail("game input did not resume")
    }

    @Test fun pauseDialogAndSettingsObserveShortcutReleasesWithoutGameInput() {
        PreferenceManager.getDefaultSharedPreferences(inst.targetContext).edit().clear()
            .putBoolean("dual_display", false)
            .putString("layout_landscape", AppSettings.LANDSCAPE_TOP_ONLY).commit()
        val menuField = GameActivity::class.java.getDeclaredField("menuDialog").apply { isAccessible = true }
        val bottomField = GameActivity::class.java.getDeclaredField("bottomToggled").apply { isAccessible = true }
        val monitor = inst.addMonitor(SettingsActivity::class.java.name, null, false)
        var settings: SettingsActivity? = null
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val firstScreen = down(KeyEvent.KEYCODE_BUTTON_THUMBL)
                val firstMenu = down(KeyEvent.KEYCODE_BUTTON_MODE)
                scenario.onActivity { activity ->
                    activity.dispatchKeyEvent(firstScreen)
                    assertTrue(bottomField.getBoolean(activity))
                    activity.dispatchKeyEvent(firstMenu)
                    val dialog = menuField.get(activity) as AlertDialog
                    // Focused dialog windows receive these directly, without
                    // passing through Activity.dispatchKeyEvent.
                    dialog.dispatchKeyEvent(up(firstScreen))
                    dialog.dispatchKeyEvent(up(firstMenu))
                    assertEquals(0, HostProbe.snapshot()[1])
                    assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                    assertTrue(dialog.listView.performItemClick(null, 0, dialog.listView.adapter.getItemId(0)))
                }
                ready(scenario)
                val secondScreen = down(firstScreen.keyCode)
                val secondMenu = down(firstMenu.keyCode)
                scenario.onActivity { activity ->
                    activity.dispatchKeyEvent(secondScreen)
                    assertFalse("dialog release did not rearm the screen shortcut", bottomField.getBoolean(activity))
                    activity.dispatchKeyEvent(secondMenu)
                    val dialog = menuField.get(activity) as AlertDialog
                    assertTrue(dialog.listView.performItemClick(null, 1, dialog.listView.adapter.getItemId(1)))
                }
                settings = inst.waitForMonitorWithTimeout(monitor, 10_000) as? SettingsActivity
                assertNotNull("Settings did not open from the pause menu", settings)
                inst.waitForIdleSync()
                inst.runOnMainSync {
                    settings!!.dispatchKeyEvent(up(secondScreen))
                    settings!!.dispatchKeyEvent(up(secondMenu))
                    assertEquals(0, HostProbe.snapshot()[1])
                    assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                    settings!!.finish()
                }
                ready(scenario)
                scenario.onActivity { activity ->
                    activity.dispatchKeyEvent(down(firstScreen.keyCode))
                    assertTrue("Settings release did not rearm the screen shortcut", bottomField.getBoolean(activity))
                    activity.dispatchKeyEvent(down(firstMenu.keyCode))
                    assertTrue("Settings release did not rearm the menu shortcut",
                        (menuField.get(activity) as? AlertDialog)?.isShowing == true)
                    assertEquals(0, HostProbe.snapshot()[1])
                    assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                }
            }
        } finally {
            inst.runOnMainSync { settings?.takeUnless { it.isFinishing }?.finish(); InputHub.clear() }
            inst.removeMonitor(monitor)
        }
    }
}
