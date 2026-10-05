package com.emerald3ds.android

import android.graphics.Rect
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.FrameLayout
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.DialogFragment
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceManager
import androidx.preference.PreferenceScreen
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** These dialogs own a Window, so their input bypasses SettingsActivity.dispatchKeyEvent. */
@RunWith(AndroidJUnit4::class)
class SettingsPreferenceInputTest {
    private val inst = InstrumentationRegistry.getInstrumentation()

    @Before fun prepare() {
        check(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(inst.targetContext).edit().clear()
            .putBoolean("dual_display", false)
            .putBoolean("qol_fast_forward", true)
            .putString("filter", "nearest").commit()
        inst.runOnMainSync { InputHub.clear() }
    }

    private fun down(code: Int): KeyEvent {
        SystemClock.sleep(2)
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0, 0, 994, 0,
            KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_GAMEPAD)
    }

    private fun up(press: KeyEvent) = KeyEvent(press.downTime, SystemClock.uptimeMillis(),
        KeyEvent.ACTION_UP, press.keyCode, 0, 0, press.deviceId, 0,
        KeyEvent.FLAG_FROM_SYSTEM, press.source)

    private fun trigger(value: Float, dispatch: (MotionEvent) -> Unit) {
        val now = SystemClock.uptimeMillis()
        val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
            arrayOf(MotionEvent.PointerCoords().apply { setAxisValue(MotionEvent.AXIS_RTRIGGER, value) }),
            0, 0, 1f, 1f, 994, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try { dispatch(event) } finally { event.recycle() }
    }

    private fun ready(scenario: ActivityScenario<GameActivity>) {
        val deadline = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < deadline) {
            var enabled = false
            scenario.onActivity {
                enabled = (it.findViewById<FrameLayout>(R.id.overlay_container)
                    .getChildAt(0) as ControlsOverlayView).inputEnabled
            }
            if (enabled && HostProbe.snapshot()[0] == NativeBridge.STATE_RUNNING) return
            SystemClock.sleep(30)
        }
        fail("game input did not resume")
    }

    private fun menu(activity: GameActivity) = GameActivity::class.java.getDeclaredField("menuDialog")
        .apply { isAccessible = true }.get(activity) as? AlertDialog

    private fun fast(activity: GameActivity) = GameActivity::class.java.getDeclaredField("fastForward")
        .apply { isAccessible = true }.get(activity) as FastForwardState

    private fun preferenceDialog(settings: SettingsActivity): AlertDialog =
        settings.supportFragmentManager.fragments.filterIsInstance<DialogFragment>()
            .mapNotNull { it.dialog as? AlertDialog }.single { it.isShowing }

    private enum class Destination { PREFERENCE, RECREATED_PREFERENCE, MYSTERY }

    @Test fun listPreferenceWindowReleasesRearmShortcutsAndSelectionStillWorks() =
        releaseThroughDialog(Destination.PREFERENCE)

    @Test fun restoredPreferenceWindowAlsoObservesReleases() =
        releaseThroughDialog(Destination.RECREATED_PREFERENCE)

    @Test fun mysteryDetailsWindowReleasesRearmShortcutsWithoutActivatingEvent() =
        releaseThroughDialog(Destination.MYSTERY)

    private fun releaseThroughDialog(destination: Destination) {
        val monitor = inst.addMonitor(SettingsActivity::class.java.name, null, false)
        var settings: SettingsActivity? = null
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val r2 = down(KeyEvent.KEYCODE_BUTTON_R2)
                val guide = down(KeyEvent.KEYCODE_BUTTON_MODE)
                val left = down(KeyEvent.KEYCODE_BUTTON_L1)
                val right = down(KeyEvent.KEYCODE_BUTTON_R1)
                scenario.onActivity {
                    it.dispatchKeyEvent(r2)
                    trigger(1f) { event -> it.dispatchGenericMotionEvent(event) }
                    assertTrue(fast(it).toggled)
                    it.dispatchKeyEvent(guide)
                    val pause = menu(it)!!
                    pause.dispatchKeyEvent(left)
                    pause.dispatchKeyEvent(right)
                    pause.listView.performItemClick(null, 1, pause.listView.adapter.getItemId(1))
                }
                settings = inst.waitForMonitorWithTimeout(monitor, 10_000) as? SettingsActivity
                assertNotNull("Settings did not open", settings)
                inst.waitForIdleSync()
                inst.runOnMainSync {
                    val activity = settings!!
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    if (destination == Destination.MYSTERY) {
                        activity.onPreferenceStartScreen(fragment,
                            fragment.findPreference<PreferenceScreen>("mystery_events")!!)
                        activity.supportFragmentManager.executePendingTransactions()
                        val events = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                            as MysteryEventsFragment
                        val event = events.findPreference<Preference>("mystery_event_0")!!
                        event.onPreferenceClickListener!!.onPreferenceClick(event)
                    } else {
                        fragment.onDisplayPreferenceDialog(fragment.findPreference<ListPreference>("filter")!!)
                        activity.supportFragmentManager.executePendingTransactions()
                    }
                }
                inst.waitForIdleSync()
                if (destination == Destination.RECREATED_PREFERENCE) {
                    val recreated = inst.addMonitor(SettingsActivity::class.java.name, null, false)
                    try {
                        inst.runOnMainSync { settings!!.recreate() }
                        settings = inst.waitForMonitorWithTimeout(recreated, 10_000) as? SettingsActivity
                        assertNotNull("Settings was not recreated", settings)
                        inst.waitForIdleSync()
                    } finally { inst.removeMonitor(recreated) }
                }
                inst.runOnMainSync {
                    val activity = settings!!
                    val shown = if (destination == Destination.MYSTERY) {
                        val events = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                            as MysteryEventsFragment
                        MysteryEventsFragment::class.java.getDeclaredField("dialog")
                            .apply { isAccessible = true }.get(events) as AlertDialog
                    } else preferenceDialog(activity)
                    assertTrue("dialog window is not visible", shown.isShowing)
                    assertTrue(shown.window!!.decorView.getGlobalVisibleRect(Rect()))
                    val focused = if (destination == Destination.MYSTERY)
                        shown.getButton(AlertDialog.BUTTON_NEGATIVE) else shown.listView
                    focused.isFocusableInTouchMode = true
                    assertTrue("dialog child could not take focus", focused.requestFocus())
                    var childReceivedNeutral = false
                    focused.setOnGenericMotionListener { _, event ->
                        childReceivedNeutral = event.getAxisValue(MotionEvent.AXIS_RTRIGGER) == 0f
                        true // A focused child may consume joystick motion.
                    }
                    val callback = shown.window!!.callback
                    listOf(r2, guide, left, right).forEach { callback.dispatchKeyEvent(up(it)) }
                    trigger(0f) { callback.dispatchGenericMotionEvent(it) }
                    assertTrue("input observation consumed the dialog child's motion", childReceivedNeutral)
                    assertEquals("a release reached gameplay while Settings was open", 0, HostProbe.snapshot()[1])
                    assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                    if (destination == Destination.MYSTERY) {
                        shown.getButton(AlertDialog.BUTTON_NEGATIVE).performClick()
                    } else {
                        assertTrue(shown.listView.isShown)
                        assertTrue(shown.listView.performItemClick(null, 1, shown.listView.adapter.getItemId(1)))
                    }
                }
                inst.waitForIdleSync()
                inst.runOnMainSync {
                    if (destination != Destination.MYSTERY)
                        assertEquals("normal preference selection was consumed", "linear",
                            PreferenceManager.getDefaultSharedPreferences(settings!!).getString("filter", null))
                    settings!!.finish()
                }
                ready(scenario)
                scenario.onActivity {
                    val next = down(KeyEvent.KEYCODE_BUTTON_R2)
                    it.dispatchKeyEvent(next)
                    trigger(1f) { event -> it.dispatchGenericMotionEvent(event) }
                    assertFalse("$destination lost the R2 digital/analog release", fast(it).toggled)
                    it.dispatchKeyEvent(up(next))
                    trigger(0f) { event -> it.dispatchGenericMotionEvent(event) }
                    it.dispatchKeyEvent(down(KeyEvent.KEYCODE_BUTTON_L1))
                    it.dispatchKeyEvent(down(KeyEvent.KEYCODE_BUTTON_R1))
                    assertTrue("$destination lost a shoulder release", menu(it)?.isShowing == true)
                    menu(it)!!.dismiss()
                }
                ready(scenario)
                scenario.onActivity {
                    it.dispatchKeyEvent(down(KeyEvent.KEYCODE_BUTTON_MODE))
                    assertTrue("$destination lost the Guide release", menu(it)?.isShowing == true)
                    menu(it)!!.dismiss()
                }
                ready(scenario)
                scenario.onActivity {
                    it.dispatchKeyEvent(down(KeyEvent.KEYCODE_BUTTON_MODE))
                    assertNull("a held Guide repeat reopened pause", menu(it))
                }
            }
        } finally {
            inst.runOnMainSync { settings?.takeUnless { it.isFinishing }?.finish(); InputHub.clear() }
            inst.removeMonitor(monitor)
        }
    }
}
