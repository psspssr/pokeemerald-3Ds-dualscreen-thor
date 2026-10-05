package com.emerald3ds.android

import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.SeekBar
import androidx.appcompat.app.AlertDialog
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real Window callback input and real Android focus/click behavior. */
@RunWith(AndroidJUnit4::class)
class MenuNavigationTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val device = 997

    @Before fun prepare() {
        check(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(inst.targetContext).edit().clear()
            .putBoolean("dual_display", false).putBoolean("qol_fast_forward", true).commit()
        cleanup()
    }

    @After fun cleanup() = inst.runOnMainSync {
        MenuNavigation.removeDevice(device)
        MenuNavigation.removeDevice(KeyCharacterMap.VIRTUAL_KEYBOARD)
        InputHub.clear()
    }

    private fun down(code: Int, keyboard: Boolean = false, repeat: Int = 0, metaState: Int = 0): KeyEvent {
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, repeat, metaState,
            if (keyboard) KeyCharacterMap.VIRTUAL_KEYBOARD else device, 0, KeyEvent.FLAG_FROM_SYSTEM,
            if (keyboard) InputDevice.SOURCE_KEYBOARD else InputDevice.SOURCE_GAMEPAD)
    }

    private fun up(key: KeyEvent, canceled: Boolean = false) = KeyEvent(key.downTime, SystemClock.uptimeMillis(),
        KeyEvent.ACTION_UP, key.keyCode, 0, key.metaState, key.deviceId, 0,
        KeyEvent.FLAG_FROM_SYSTEM or (if (canceled) KeyEvent.FLAG_CANCELED else 0), key.source)

    private fun tap(dialog: AlertDialog, code: Int, keyboard: Boolean = false) {
        val key = down(code, keyboard)
        dialog.window!!.callback.dispatchKeyEvent(key)
        dialog.window!!.callback.dispatchKeyEvent(up(key))
    }

    private fun motion(y: Float = 0f, x: Float = 0f, hatY: Float = 0f,
                       r2: Float = 0f, rightX: Float = 0f, dispatch: (MotionEvent) -> Unit) {
        val now = SystemClock.uptimeMillis()
        val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
            arrayOf(MotionEvent.PointerCoords().apply {
                setAxisValue(MotionEvent.AXIS_X, x); setAxisValue(MotionEvent.AXIS_Y, y)
                setAxisValue(MotionEvent.AXIS_HAT_Y, hatY)
                setAxisValue(MotionEvent.AXIS_RTRIGGER, r2); setAxisValue(MotionEvent.AXIS_Z, rightX)
            }), 0, 0, 1f, 1f, device, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try { dispatch(event) } finally { event.recycle() }
    }

    private fun waitUntil(message: String, condition: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < end) {
            if (condition()) return
            SystemClock.sleep(25)
        }
        fail(message)
    }

    private fun ready(scenario: ActivityScenario<GameActivity>) = waitUntil("game did not resume") {
        var enabled = false
        scenario.onActivity {
            enabled = (it.findViewById<FrameLayout>(R.id.overlay_container)
                .getChildAt(0) as ControlsOverlayView).inputEnabled
        }
        enabled && HostProbe.snapshot()[0] == NativeBridge.STATE_RUNNING
    }

    private fun menu(activity: GameActivity) = GameActivity::class.java.getDeclaredField("menuDialog")
        .apply { isAccessible = true }.get(activity) as? AlertDialog

    private fun fast(activity: GameActivity) = GameActivity::class.java.getDeclaredField("fastForward")
        .apply { isAccessible = true }.get(activity) as FastForwardState

    private fun openPause(scenario: ActivityScenario<GameActivity>): AlertDialog {
        ready(scenario)
        scenario.onActivity { it.onMenuKey() }
        lateinit var shown: AlertDialog
        waitUntil("Pause has no visible initial Resume selection") {
            var selected = false
            scenario.onActivity {
                menu(it)?.let { dialog ->
                    shown = dialog
                    selected = dialog.isShowing && dialog.listView.isShown && dialog.listView.hasFocus() &&
                        dialog.listView.selectedItemPosition == 0
                }
            }
            selected
        }
        assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
        return shown
    }

    @Test fun thorRightConfirmsSettingsAndBottomReturnsWithoutLeakingHeldInput() {
        val monitor = inst.addMonitor(SettingsActivity::class.java.name, null, false)
        var settings: SettingsActivity? = null
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                val pause = openPause(scenario)
                val confirm = down(KeyEvent.KEYCODE_BUTTON_B)
                scenario.onActivity {
                    tap(pause, KeyEvent.KEYCODE_DPAD_DOWN, keyboard = true)
                    assertEquals(1, pause.listView.selectedItemPosition)
                    pause.window!!.callback.dispatchKeyEvent(confirm)
                    pause.window!!.callback.dispatchKeyEvent(down(confirm.keyCode))
                    assertTrue("a held confirm activated before release", pause.isShowing)
                    pause.window!!.callback.dispatchKeyEvent(up(confirm))
                }
                settings = inst.waitForMonitorWithTimeout(monitor, 10_000) as? SettingsActivity
                assertNotNull("Thor right face backed out instead of opening Settings", settings)
                inst.waitForIdleSync()
                inst.runOnMainSync {
                    val callback = settings!!.window.callback
                    val canceled = down(KeyEvent.KEYCODE_BUTTON_A)
                    callback.dispatchKeyEvent(canceled)
                    callback.dispatchKeyEvent(up(canceled, canceled = true))
                    callback.dispatchKeyEvent(down(canceled.keyCode))
                    assertFalse("canceled/held Back closed Settings", settings!!.isFinishing)
                    callback.dispatchKeyEvent(up(canceled))
                    val back = down(KeyEvent.KEYCODE_BUTTON_A)
                    callback.dispatchKeyEvent(back)
                    assertFalse("Back activated before release", settings!!.isFinishing)
                    callback.dispatchKeyEvent(up(back))
                }
                ready(scenario)
                scenario.onActivity {
                    assertEquals("menu actions leaked into gameplay", 0, InputHub.sentKeys)
                    val fresh = down(KeyEvent.KEYCODE_BUTTON_A)
                    it.dispatchKeyEvent(fresh)
                    assertTrue("real release did not rearm gameplay", InputHub.sentKeys and CtrKeys.B != 0)
                    it.dispatchKeyEvent(up(fresh))
                }
            }
        } finally {
            inst.runOnMainSync { settings?.takeUnless { it.isFinishing }?.finish() }
            inst.removeMonitor(monitor)
        }
    }

    @Test fun labelMappingAndKeyboardConfirmResumeTheSelectedAction() {
        for ((label, code) in listOf(false to KeyEvent.KEYCODE_BUTTON_B,
            true to KeyEvent.KEYCODE_BUTTON_A, false to KeyEvent.KEYCODE_ENTER,
            false to KeyEvent.KEYCODE_DPAD_CENTER)) {
            PreferenceManager.getDefaultSharedPreferences(inst.targetContext).edit()
                .putString("gamepad_mapping", if (label) "label" else "position").commit()
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                val pause = openPause(scenario)
                scenario.onActivity { tap(pause, code, keyboard = code == KeyEvent.KEYCODE_ENTER) }
                ready(scenario)
                assertEquals(0, InputHub.sentKeys)
            }
            cleanup()
        }
    }

    @Test fun rapidConfirmThroughInputDispatcherOpensSettingsAndTheFirstNextPage() {
        val monitor = inst.addMonitor(SettingsActivity::class.java.name, null, false)
        var settings: SettingsActivity? = null
        fun confirm() {
            val now = SystemClock.uptimeMillis()
            for (action in listOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP))
                assertTrue(inst.uiAutomation.injectInputEvent(KeyEvent(now, now + action, action,
                    KeyEvent.KEYCODE_BUTTON_B, 0, 0, KeyCharacterMap.VIRTUAL_KEYBOARD, 0,
                    KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_GAMEPAD), true))
        }
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                val pause = openPause(scenario)
                scenario.onActivity { tap(pause, KeyEvent.KEYCODE_DPAD_DOWN) }
                // These keys go through Android's real window handoff. Calling
                // old/new callbacks manually cannot detect a dropped key UP.
                confirm()
                settings = inst.waitForMonitorWithTimeout(monitor, 10_000) as? SettingsActivity
                assertNotNull(settings)
                waitUntil("Settings window did not gain focus") {
                    var focused = false
                    inst.runOnMainSync { focused = settings!!.window.decorView.hasWindowFocus() }
                    focused
                }
                inst.waitForIdleSync()
                confirm()
                waitUntil("first fresh confirm was lost during the Pause-to-Settings handoff") {
                    var opened = false
                    inst.runOnMainSync { opened = settings!!.supportFragmentManager.backStackEntryCount == 1 }
                    opened
                }
                assertEquals(0, InputHub.sentKeys)
                inst.runOnMainSync { settings!!.finish() }
                ready(scenario)
            }
        } finally {
            inst.runOnMainSync { settings?.takeUnless { it.isFinishing }?.finish() }
            inst.removeMonitor(monitor)
        }
    }

    @Test fun everyPauseActionIsReachableAndFastForwardCanBeConfirmed() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            val pause = openPause(scenario)
            scenario.onActivity { activity ->
                val list = pause.listView
                for (position in 1 until list.count) {
                    tap(pause, KeyEvent.KEYCODE_DPAD_DOWN, keyboard = true)
                    assertEquals(position, list.selectedItemPosition)
                }
                assertEquals(activity.getString(R.string.menu_quit), list.adapter.getItem(2).toString())
                for (position in list.count - 2 downTo 0) {
                    tap(pause, KeyEvent.KEYCODE_DPAD_UP, keyboard = true)
                    assertEquals(position, list.selectedItemPosition)
                }
                val label = activity.getString(R.string.fast_forward_start, fast(activity).options.fastForwardSpeed)
                val fastIndex = (0 until list.count).single { list.adapter.getItem(it).toString() == label }
                repeat(fastIndex) { tap(pause, KeyEvent.KEYCODE_DPAD_DOWN) }
                tap(pause, KeyEvent.KEYCODE_BUTTON_B)
            }
            ready(scenario)
            scenario.onActivity { assertTrue("controller confirm did not toggle optional fast-forward", fast(it).toggled) }
        }
    }

    @Test fun escapeCancelsOnceAndARealReleaseAllowsTheNextPause() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            val pause = openPause(scenario)
            val escape = down(KeyEvent.KEYCODE_ESCAPE, keyboard = true)
            scenario.onActivity {
                pause.window!!.callback.dispatchKeyEvent(escape)
                pause.window!!.callback.dispatchKeyEvent(down(KeyEvent.KEYCODE_ESCAPE, keyboard = true))
                assertTrue("held Escape canceled before release", pause.isShowing)
                pause.window!!.callback.dispatchKeyEvent(up(escape))
            }
            ready(scenario)
            SystemClock.sleep(2)
            scenario.onActivity {
                val fresh = down(KeyEvent.KEYCODE_ESCAPE, keyboard = true)
                it.dispatchKeyEvent(fresh)
                assertTrue(menu(it)?.isShowing == true)
                menu(it)!!.window!!.callback.dispatchKeyEvent(up(fresh))
                assertTrue("the opening shortcut UP canceled its own menu", menu(it)?.isShowing == true)
            }
        }
    }

    private fun listDialog(activity: GameActivity, choose: (Int) -> Unit = {}): AlertDialog =
        MaterialAlertDialogBuilder(activity).setTitle("Navigation fixture")
            .setItems(Array(12) { "Row $it" }) { _, which -> choose(which) }
            .setNegativeButton("Cancel", null).show().also {
                observePausedGameInput(it)
                MenuNavigation.refreshFocus(it.window!!, force = true)
            }

    private fun selected(dialog: AlertDialog): Int {
        var result = -1
        inst.runOnMainSync { result = dialog.listView.selectedItemPosition }
        return result
    }

    private fun listReady(dialog: AlertDialog): Boolean {
        var ready = false
        inst.runOnMainSync {
            ready = dialog.window!!.decorView.hasWindowFocus() && dialog.listView.hasFocus() &&
                dialog.listView.selectedItemPosition == 0
        }
        return ready
    }

    @Test fun sticksAndHatsMergeWithKeysRepeatBoundedlyAndStopAtNeutral() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            openPause(scenario)
            lateinit var dialog: AlertDialog
            scenario.onActivity { dialog = listDialog(it) }
            try {
                waitUntil("fixture has no visible initial focus") { listReady(dialog) }
                val digital = down(KeyEvent.KEYCODE_DPAD_DOWN)
                inst.runOnMainSync {
                    for (drift in listOf(0.05f, 0.25f, 0.55f))
                        motion(y = drift) { dialog.window!!.callback.dispatchGenericMotionEvent(it) }
                    assertEquals("neutral stick drift moved focus", 0, dialog.listView.selectedItemPosition)
                    motion(hatY = 1f) { dialog.window!!.callback.dispatchGenericMotionEvent(it) }
                    assertEquals(1, dialog.listView.selectedItemPosition)
                    dialog.window!!.callback.dispatchKeyEvent(digital)
                    repeat(8) { dialog.window!!.callback.dispatchKeyEvent(down(digital.keyCode)) }
                    assertEquals("duplicate hat/key reports moved twice", 1, dialog.listView.selectedItemPosition)
                }
                waitUntil("held direction did not repeat") { selected(dialog) >= 3 }
                var stopped = -1
                inst.runOnMainSync {
                    dialog.window!!.callback.dispatchKeyEvent(up(digital))
                    motion { dialog.window!!.callback.dispatchGenericMotionEvent(it) }
                    stopped = dialog.listView.selectedItemPosition
                }
                SystemClock.sleep(400)
                assertEquals("neutral stick kept repeating", stopped, selected(dialog))
                assertEquals(0, HostProbe.snapshot()[1])
            } finally { inst.runOnMainSync { dialog.dismiss() } }
        }
    }

    @Test fun heldStickDoesNotDriftIntoTheNextDialog() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            openPause(scenario)
            lateinit var first: AlertDialog
            var second: AlertDialog? = null
            scenario.onActivity { activity -> first = listDialog(activity) { second = listDialog(activity) } }
            try {
                waitUntil("first list did not focus") { listReady(first) }
                inst.runOnMainSync {
                    motion(y = 1f) { first.window!!.callback.dispatchGenericMotionEvent(it) }
                    tap(first, KeyEvent.KEYCODE_BUTTON_B)
                }
                waitUntil("second dialog did not open") {
                    var shown = false
                    inst.runOnMainSync { shown = second?.isShowing == true }
                    shown
                }
                waitUntil("second dialog did not focus") { listReady(second!!) }
                SystemClock.sleep(500)
                inst.runOnMainSync { motion(y = 1f) { second!!.window!!.callback.dispatchGenericMotionEvent(it) } }
                assertEquals("old window's held direction advanced new focus", 0, selected(second!!))
                inst.runOnMainSync {
                    motion { second!!.window!!.callback.dispatchGenericMotionEvent(it) }
                    motion(y = 1f) { second!!.window!!.callback.dispatchGenericMotionEvent(it) }
                }
                assertEquals(1, selected(second!!))
            } finally { inst.runOnMainSync { second?.dismiss(); first.dismiss() } }
        }
    }

    @Test fun confirmWithinTheSameWindowDoesNotRestartHeldNavigation() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            openPause(scenario)
            lateinit var dialog: AlertDialog
            scenario.onActivity { activity ->
                dialog = MaterialAlertDialogBuilder(activity)
                    .setSingleChoiceItems(Array(12) { "Row $it" }, 0) { window, _ ->
                        // This action retains its Window and establishes a
                        // new selection, just as a Settings page transition.
                        (window as AlertDialog).listView.setSelection(0)
                    }.setNegativeButton("Cancel", null).show()
                observePausedGameInput(dialog)
                MenuNavigation.refreshFocus(dialog.window!!, force = true)
            }
            try {
                waitUntil("single-choice list did not focus") { listReady(dialog) }
                inst.runOnMainSync {
                    motion(y = 1f) { dialog.window!!.callback.dispatchGenericMotionEvent(it) }
                    assertEquals(1, dialog.listView.selectedItemPosition)
                    tap(dialog, KeyEvent.KEYCODE_BUTTON_B)
                }
                inst.waitForIdleSync()
                assertTrue(dialog.isShowing)
                assertEquals("confirm UP restarted the old held direction", 0, selected(dialog))
                SystemClock.sleep(450)
                inst.runOnMainSync { motion(y = 1f) { dialog.window!!.callback.dispatchGenericMotionEvent(it) } }
                assertEquals("same-window content inherited the held stick", 0, selected(dialog))
                inst.runOnMainSync {
                    motion { dialog.window!!.callback.dispatchGenericMotionEvent(it) }
                    motion(y = 1f) { dialog.window!!.callback.dispatchGenericMotionEvent(it) }
                }
                assertEquals(1, selected(dialog))
            } finally { inst.runOnMainSync { dialog.dismiss() } }
        }
    }

    @Test fun menuStickGuardPreservesFreshTriggersAndRightStickInTheSameGameReport() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            val pause = openPause(scenario)
            scenario.onActivity {
                // Hold Up at the first row, then resume without releasing it.
                motion(y = -1f) { event -> pause.window!!.callback.dispatchGenericMotionEvent(event) }
                tap(pause, KeyEvent.KEYCODE_BUTTON_B)
            }
            ready(scenario)
            scenario.onActivity {
                motion(y = -1f, r2 = 1f, rightX = 1f) { event -> it.dispatchGenericMotionEvent(event) }
                assertTrue("fresh R2 was swallowed with the held menu stick", fast(it).toggled)
                assertTrue(InputHub.sentKeys and CtrKeys.CSTICK_RIGHT != 0)
                assertEquals("held menu stick leaked into gameplay", 0,
                    InputHub.sentKeys and (CtrKeys.CPAD_UP or CtrKeys.CPAD_DOWN))
                motion(y = -1f) { event -> it.dispatchGenericMotionEvent(event) } // release R2 only
                motion(r2 = 1f, rightX = -1f) { event -> it.dispatchGenericMotionEvent(event) } // navigation neutral + fresh R2
                assertFalse("neutral menu-stick report swallowed the next R2", fast(it).toggled)
                assertTrue(InputHub.sentKeys and CtrKeys.CSTICK_LEFT != 0)
                motion { event -> it.dispatchGenericMotionEvent(event) }
                assertEquals(0, InputHub.sentKeys)
            }
        }
    }

    @Test fun textEditingAndSeekBarKeepTheirNativeKeyBehavior() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            openPause(scenario)
            lateinit var dialog: AlertDialog
            lateinit var edit: EditText
            lateinit var slider: SeekBar
            scenario.onActivity { activity ->
                edit = EditText(activity).apply { showSoftInputOnFocus = false; setSingleLine() }
                slider = SeekBar(activity).apply { max = 10; progress = 5 }
                val content = LinearLayout(activity).apply {
                    orientation = LinearLayout.VERTICAL
                    addView(edit); addView(slider)
                }
                dialog = MaterialAlertDialogBuilder(activity).setView(content)
                    .setNegativeButton("Cancel", null).setPositiveButton("OK", null).show()
                observePausedGameInput(dialog)
            }
            inst.waitForIdleSync()
            try {
                inst.runOnMainSync {
                    assertTrue(edit.requestFocusFromTouch())
                    tap(dialog, KeyEvent.KEYCODE_X, keyboard = true)
                    tap(dialog, KeyEvent.KEYCODE_Z, keyboard = true)
                    assertEquals("text X/Z became confirm/back actions", "xz", edit.text.toString())
                    assertTrue(dialog.isShowing)
                    edit.setSelection(edit.text.length)
                    val shift = down(KeyEvent.KEYCODE_SHIFT_LEFT, keyboard = true)
                    dialog.window!!.callback.dispatchKeyEvent(shift)
                    val select = down(KeyEvent.KEYCODE_DPAD_LEFT, keyboard = true, metaState = KeyEvent.META_SHIFT_ON)
                    dialog.window!!.callback.dispatchKeyEvent(select)
                    dialog.window!!.callback.dispatchKeyEvent(up(select))
                    dialog.window!!.callback.dispatchKeyEvent(up(shift))
                    assertEquals("keyboard arrow lost its Shift selection modifier", 1,
                        minOf(edit.selectionStart, edit.selectionEnd))
                    assertEquals(2, maxOf(edit.selectionStart, edit.selectionEnd))
                    assertTrue(slider.requestFocusFromTouch())
                    tap(dialog, KeyEvent.KEYCODE_DPAD_RIGHT, keyboard = true)
                    assertEquals(6, slider.progress)
                    tap(dialog, KeyEvent.KEYCODE_DPAD_LEFT, keyboard = true)
                    assertEquals(5, slider.progress)
                }
            } finally { inst.runOnMainSync { dialog.dismiss() } }
        }
    }
}
