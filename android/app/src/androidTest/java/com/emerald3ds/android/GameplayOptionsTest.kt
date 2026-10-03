package com.emerald3ds.android

import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.appcompat.app.AlertDialog
import androidx.preference.ListPreference
import androidx.preference.ListPreferenceDialogFragmentCompat
import androidx.preference.PreferenceManager
import androidx.preference.PreferenceScreen
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.appbar.MaterialToolbar
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GameplayOptionsTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    @Before fun defaults() {
        PreferenceManager.getDefaultSharedPreferences(context).edit().clear().commit()
        instrumentation.runOnMainSync { InputHub.clear() }
    }

    @Test fun optionsAreOptInAndSelectorIsExactlyTwoOrFour() {
        val options = GameplayOptions.load(context)
        assertEquals(GameplayOptions(), options)
        assertArrayEquals(arrayOf("2", "4"), context.resources.getStringArray(R.array.qol_speed_values))
        val state = FastForwardState { }
        state.configure(options)
        state.toggle(); state.hold(true)
        assertEquals(1, state.speed)
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString("qol_speed", "3")
            .putString("qol_shiny", "999").commit()
        assertEquals(4, GameplayOptions.load(context).fastForwardSpeed)
        assertEquals(1, GameplayOptions.load(context).shinyMultiplier)
    }

    @Test fun shinyOddsPickerPersistsEachDisplayedRateAcrossRecreation() {
        val labels = arrayOf("Original — 1 in 8,192", "About 1 in 2,048",
            "About 1 in 512", "About 1 in 256", "About 1 in 128")
        val denominators = intArrayOf(8192, 2048, 512, 256, 128)
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val root = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                    as SettingsActivity.SettingsFragment
                assertTrue(activity.onPreferenceStartScreen(root, root.findPreference<PreferenceScreen>("qol")!!))
                activity.supportFragmentManager.executePendingTransactions()
                assertEquals(1, GameplayOptions.load(context).shinyMultiplier)
            }
            // Exercise the real list-dialog callback and persisted preference,
            // including the new 1/256 choice and returning to original odds.
            for (index in listOf(3, 1, 2, 4, 0)) {
                scenario.onActivity { activity ->
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    fragment.onDisplayPreferenceDialog(fragment.findPreference<ListPreference>("qol_shiny")!!)
                    activity.supportFragmentManager.executePendingTransactions()
                    val chooser = activity.supportFragmentManager.fragments
                        .filterIsInstance<ListPreferenceDialogFragmentCompat>().single()
                    val list = (chooser.requireDialog() as AlertDialog).listView
                    assertEquals(5, list.adapter.count)
                    for (row in labels.indices) assertEquals(labels[row], list.adapter.getItem(row).toString())
                    assertTrue(list.performItemClick(null, index, list.adapter.getItemId(index)))
                }
                instrumentation.waitForIdleSync()
                scenario.recreate()
                scenario.onActivity { activity ->
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    val preference = fragment.findPreference<ListPreference>("qol_shiny")!!
                    assertEquals(labels[index], preference.summary.toString())
                    assertEquals(denominators[index], 8192 / GameplayOptions.load(context).shinyMultiplier)
                }
            }
        }
    }

    private fun key(input: PhysicalInput, code: Int, down: Boolean, repeat: Int = 0, device: Int = 7): KeyEvent {
        val now = SystemClock.uptimeMillis()
        val event = KeyEvent(now, now, if (down) KeyEvent.ACTION_DOWN else KeyEvent.ACTION_UP,
            code, repeat, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD)
        assertTrue(input.onKey(event))
        return event
    }

    private fun axes(input: PhysicalInput, left: Float, right: Float, device: Int = 7) {
        val now = SystemClock.uptimeMillis()
        val coordinates = MotionEvent.PointerCoords().apply {
            setAxisValue(MotionEvent.AXIS_LTRIGGER, left)
            setAxisValue(MotionEvent.AXIS_RTRIGGER, right)
        }
        val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }), arrayOf(coordinates),
            0, 0, 1f, 1f, device, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try { assertTrue(input.onMotion(event)) } finally { event.recycle() }
    }

    @Test fun thorTriggersMergeDigitalAndAnalogAndHoldReleasesCleanly() {
        instrumentation.runOnMainSync {
            val state = FastForwardState { }
            var toggles = 0
            val input = PhysicalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++; state.toggle() }
                override fun onFastForwardHold(held: Boolean) = state.hold(held)
            })
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            assertEquals(1, state.speed)
            assertEquals(CtrKeys.ZR, InputHub.sentKeys)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            state.configure(GameplayOptions(fastForwardEnabled = true))
            input.fastForwardEnabled = true
            val originalPress = key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true, repeat = 1)
            axes(input, 0f, 1f)
            assertEquals(1, toggles)
            assertEquals(4, state.speed)
            assertEquals(0, InputHub.sentKeys and (CtrKeys.ZL or CtrKeys.ZR))
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            axes(input, 0f, 0.49f); axes(input, 0f, 0.51f)
            assertEquals(1, toggles) // Analog jitter cannot toggle twice.
            input.clear() // A held R2 reported again after resume is not a new press.
            axes(input, 0f, 1f)
            assertTrue(input.onKey(KeyEvent(originalPress)))
            assertEquals(1, toggles)
            assertEquals(4, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            axes(input, 0f, 0f)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            assertEquals(1, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            key(input, KeyEvent.KEYCODE_TAB, true, device = 8)
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            assertEquals(4, state.speed)
            input.removeDevice(8)
            assertEquals(1, state.speed)
            state.configure(GameplayOptions(fastForwardEnabled = true, fastForwardSpeed = 2))
            axes(input, 1f, 0f)
            assertEquals(2, state.speed)
            input.clear()
            assertEquals(1, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            assertEquals(2, state.speed)
            state.configure(GameplayOptions())
            input.fastForwardEnabled = false
            assertEquals(1, state.speed)
            input.clear()
        }
    }

    @Test fun gameplayScreenOpensAndReturnsToSettings() {
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val root = activity.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
                val screen = root.findPreference<PreferenceScreen>("qol")!!
                assertTrue(activity.onPreferenceStartScreen(root, screen))
                activity.supportFragmentManager.executePendingTransactions()
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
                assertEquals("qol", fragment.preferenceScreen.key)
                assertEquals(context.getString(R.string.qol_title), activity.findViewById<MaterialToolbar>(R.id.toolbar).title)
                assertFalse(GameplayOptions.load(context).fastForwardEnabled)
                assertTrue(activity.supportFragmentManager.popBackStackImmediate())
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertEquals(context.getString(R.string.settings_title), activity.findViewById<MaterialToolbar>(R.id.toolbar).title)
            }
        }
    }

    @Test fun heldAnalogR2RequiresFullReleaseAfterPause() {
        instrumentation.runOnMainSync {
            var toggles = 0
            val input = PhysicalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++ }
            })
            input.fastForwardEnabled = true
            axes(input, 0f, 0.6f)
            assertEquals(1, toggles)
            input.clear()
            // Still inside the pressed side of the hysteresis band. This
            // is the same physical hold being replayed after the menu.
            axes(input, 0f, 0.5f)
            axes(input, 0f, 0.6f)
            assertEquals("R2 toggled without reaching its release threshold", 1, toggles)
            axes(input, 0f, 0.39f)
            axes(input, 0f, 0.6f)
            assertEquals(2, toggles)
            input.clear()
        }
    }

    @Test fun anotherControllersNeutralFrameCannotRearmHeldR2() {
        instrumentation.runOnMainSync {
            var toggles = 0
            val input = PhysicalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++ }
            })
            input.fastForwardEnabled = true
            axes(input, 0f, 1f, device = 7)
            input.clear()
            axes(input, 0f, 0f, device = 8)
            axes(input, 0f, 1f, device = 7)
            assertEquals("another controller rearmed the held trigger", 1, toggles)
            input.removeDevice(7)
            axes(input, 0f, 1f, device = 8)
            assertEquals(2, toggles)
            input.clear()
        }
    }

    @Test fun unrelatedKeyReleaseCannotRearmButFreshDigitalR2Can() {
        instrumentation.runOnMainSync {
            var toggles = 0
            val input = PhysicalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++ }
            })
            input.fastForwardEnabled = true
            val previousPress = key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            input.clear()
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            assertTrue(input.onKey(KeyEvent(previousPress)))
            assertEquals(1, toggles)
            // Its physical release may have reached the dialog instead of
            // the game. A fresh key downTime is still a reliable new edge.
            SystemClock.sleep(2)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            axes(input, 0f, 1f)
            assertEquals(2, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            axes(input, 0f, 0f)
            input.clear()
        }
    }
}
