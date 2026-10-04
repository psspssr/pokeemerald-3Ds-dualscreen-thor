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
import androidx.preference.SwitchPreferenceCompat
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

    @Test fun optionsAreOptInAndSelectorOffersTwoFourEight() {
        val options = GameplayOptions.load(context)
        assertEquals(GameplayOptions(), options)
        assertArrayEquals(arrayOf("2", "4", "8"), context.resources.getStringArray(R.array.qol_speed_values))
        val state = FastForwardState { }
        state.configure(options)
        state.toggle(); state.hold(true)
        assertEquals(1, state.speed)
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString("qol_speed", "3")
            .putString("qol_shiny", "999").commit()
        assertEquals(4, GameplayOptions.load(context).fastForwardSpeed)
        assertEquals(1, GameplayOptions.load(context).shinyMultiplier)
    }

    @Test fun speedPickerPersistsAllRatesAcrossRecreation() {
        val labels = arrayOf("2×", "4×", "8×")
        val speeds = intArrayOf(2, 4, 8)
        // A saved pre-existing choice survives the newly added option.
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString("qol_speed", "2").commit()
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val root = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                    as SettingsActivity.SettingsFragment
                assertTrue(activity.onPreferenceStartScreen(root, root.findPreference<PreferenceScreen>("qol")!!))
                activity.supportFragmentManager.executePendingTransactions()
                val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                    as SettingsActivity.SettingsFragment
                assertEquals(2, GameplayOptions.load(context).fastForwardSpeed)
                assertFalse(GameplayOptions.load(context).fastForwardEnabled)
                fragment.findPreference<SwitchPreferenceCompat>("qol_fast_forward")!!.isChecked = true
            }
            for (index in listOf(2, 0, 1)) {
                scenario.onActivity { activity ->
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    val preference = fragment.findPreference<ListPreference>("qol_speed")!!
                    assertTrue(preference.isEnabled)
                    fragment.onDisplayPreferenceDialog(preference)
                    activity.supportFragmentManager.executePendingTransactions()
                    val chooser = activity.supportFragmentManager.fragments
                        .filterIsInstance<ListPreferenceDialogFragmentCompat>().single()
                    val list = (chooser.requireDialog() as AlertDialog).listView
                    assertEquals(3, list.adapter.count)
                    for (row in labels.indices) assertEquals(labels[row], list.adapter.getItem(row).toString())
                    assertTrue(list.performItemClick(null, index, list.adapter.getItemId(index)))
                }
                instrumentation.waitForIdleSync()
                scenario.recreate()
                scenario.onActivity { activity ->
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    assertEquals(labels[index], fragment.findPreference<ListPreference>("qol_speed")!!.summary.toString())
                    assertEquals(speeds[index], GameplayOptions.load(context).fastForwardSpeed)
                }
            }
        }
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

    private fun digitalInput(callbacks: PhysicalInput.Callbacks) = PhysicalInput(callbacks) { false }

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

    @Test fun eightTimesToggleAndHoldKeepTheirExistingOwnership() {
        instrumentation.runOnMainSync {
            val state = FastForwardState { }.apply {
                configure(GameplayOptions(fastForwardEnabled = true, fastForwardSpeed = 8))
            }
            val input = digitalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() = state.toggle()
                override fun onFastForwardHold(held: Boolean) = state.hold(held)
            }).apply { fastForwardEnabled = true }
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            assertEquals(8, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            assertFalse(state.toggled)
            assertEquals(8, state.speed) // L2 still owns acceleration.
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            assertEquals(1, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            assertEquals(8, state.speed)
            input.clear() // Pause clears a hold without inventing a toggle.
            assertEquals(1, state.speed)
        }
    }

    @Test fun thorTriggersMergeDigitalAndAnalogAndHoldReleasesCleanly() {
        instrumentation.runOnMainSync {
            val state = FastForwardState { }
            var toggles = 0
            val input = digitalInput(object : PhysicalInput.Callbacks {
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
            val input = digitalInput(object : PhysicalInput.Callbacks {
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
            val input = digitalInput(object : PhysicalInput.Callbacks {
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

    @Test fun unrelatedKeyOrFreshTimestampCannotReplaceObservedR2Release() {
        instrumentation.runOnMainSync {
            var toggles = 0
            val input = digitalInput(object : PhysicalInput.Callbacks {
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
            // New timestamps do not prove a physical release. Android can
            // normalize a held hardware repeat into exactly this event.
            SystemClock.sleep(2)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            assertEquals(1, toggles)
            val now = SystemClock.uptimeMillis()
            input.observeKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BUTTON_R2,
                0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD), suspended = true)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            axes(input, 0f, 1f)
            assertEquals(2, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            axes(input, 0f, 0f)
            input.clear()
        }
    }

    @Test fun heldR2RebasedByL2DoesNotToggleAgainAfterShoulderPause() {
        instrumentation.runOnMainSync {
            val state = FastForwardState { }.apply { configure(GameplayOptions(fastForwardEnabled = true)) }
            var toggles = 0
            var pauses = 0
            lateinit var input: PhysicalInput
            input = digitalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() { pauses++; input.clear() }
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++; state.toggle() }
                override fun onFastForwardHold(held: Boolean) = state.hold(held)
            }).apply { fastForwardEnabled = true }
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_L1, true)
            key(input, KeyEvent.KEYCODE_BUTTON_R1, true)
            assertEquals(1, pauses)
            assertEquals(1, toggles)
            assertEquals(4, state.speed)
            input.onInputResumed()
            SystemClock.sleep(2)
            val l2 = key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            SystemClock.sleep(2)
            // Exact uinput repro: R2 remained physically held, but another
            // key rebased its downTime and the hardware repeat count is zero.
            val repeat = KeyEvent(l2.downTime, SystemClock.uptimeMillis(), KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)
            assertTrue(input.onKey(repeat))
            assertEquals("rebased held R2 toggled fast-forward", 1, toggles)
            assertEquals(4, state.speed)
            SystemClock.sleep(2)
            val sameReport = key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            assertTrue(input.onKey(KeyEvent(sameReport.eventTime, sameReport.eventTime, KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)))
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            assertEquals("same-report timestamp collision rearmed R2", 1, toggles)
            assertEquals(4, state.speed)
            SystemClock.sleep(2)
            val normalizedTime = SystemClock.uptimeMillis()
            // Actual trace/r2-repro.txt: flags=8, scan=313, repeat=0 and
            // downTime==eventTime at a distinct time after the L2 release.
            assertTrue(input.onKey(KeyEvent(normalizedTime, normalizedTime, KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 313, KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_GAMEPAD)))
            assertEquals("normalized held R2 was mistaken for a fresh press", 1, toggles)
            assertEquals(4, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            SystemClock.sleep(2)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            assertEquals(2, toggles)
            assertEquals(1, state.speed)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            input.clear()
        }
    }

    @Test fun r2FirstPressedInDialogNeedsBothObservedReleaseChannels() {
        instrumentation.runOnMainSync {
            var toggles = 0
            val input = digitalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++ }
            }).apply { fastForwardEnabled = true }
            input.clear()
            SystemClock.sleep(2)
            val held = SystemClock.uptimeMillis()
            input.observeKeyEvent(KeyEvent(held, held, KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD), suspended = true)
            input.onInputResumed()
            SystemClock.sleep(2)
            val l2 = key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            assertTrue(input.onKey(KeyEvent(l2.eventTime, l2.eventTime, KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)))
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            assertEquals(0, toggles)
            // Neither new timestamps nor a held analog report prove release.
            SystemClock.sleep(2)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            axes(input, 0f, 1f)
            assertEquals(0, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            axes(input, 0f, 0.48f)
            assertEquals(0, toggles)
            axes(input, 0f, 0.39f)
            assertEquals("release itself toggled fast-forward", 0, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            axes(input, 0f, 1f)
            assertEquals(1, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            axes(input, 0f, 0.48f); axes(input, 0f, 0.6f)
            assertEquals(1, toggles)
            axes(input, 0f, 0.39f); axes(input, 0f, 0.6f)
            assertEquals(2, toggles)
            input.clear()
        }
    }

    @Test fun digitalR2ReleaseDuringOrJustAfterDialogDoesNotForgetHeldAnalogTrigger() {
        instrumentation.runOnMainSync {
            for (dialogOwnsUp in listOf(false, true)) for (flags in listOf(0, KeyEvent.FLAG_CANCELED)) {
                var toggles = 0
                val input = digitalInput(object : PhysicalInput.Callbacks {
                    override fun onPhysicalInput() {}
                    override fun onMenuKey() {}
                    override fun onToggleBottomScreen() {}
                    override fun onFastForwardToggle() { toggles++ }
                }).apply { fastForwardEnabled = true }
                axes(input, 0f, 0.6f)
                assertEquals(1, toggles)
                input.clear()
                // A digital UP at the controller's threshold (or focus
                // cancellation) cannot prove the analog trigger fell below
                // the app's 0.4 release threshold while its axes were cleared.
                val now = SystemClock.uptimeMillis()
                val release = KeyEvent(now, now, KeyEvent.ACTION_UP,
                    KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, flags, InputDevice.SOURCE_GAMEPAD)
                if (dialogOwnsUp) input.observeKeyEvent(release, suspended = true)
                input.onInputResumed()
                if (!dialogOwnsUp) assertTrue(input.onKey(release))
                axes(input, 0f, 0.48f); axes(input, 0f, 0.6f)
                assertEquals("dialog UP discarded analog release quarantine", 1, toggles)
                axes(input, 0f, 0.39f)
                assertEquals("neutral itself toggled fast-forward", 1, toggles)
                if (flags == KeyEvent.FLAG_CANCELED) key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
                axes(input, 0f, 0.6f)
                assertEquals(2, toggles)
                input.clear()
            }
        }
    }

    @Test fun canceledDigitalR2UpBeforeClearKeepsHeldRepeatsBlocked() {
        instrumentation.runOnMainSync {
            var toggles = 0
            val input = digitalInput(object : PhysicalInput.Callbacks {
                override fun onPhysicalInput() {}
                override fun onMenuKey() {}
                override fun onToggleBottomScreen() {}
                override fun onFastForwardToggle() { toggles++ }
            }).apply { fastForwardEnabled = true }
            val held = key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            assertEquals(1, toggles)
            // Focus cancellation may precede the Activity's clear(). It must
            // retain evidence of the hold before removing its digital key.
            assertTrue(input.onKey(KeyEvent.changeFlags(KeyEvent.changeAction(held, KeyEvent.ACTION_UP), KeyEvent.FLAG_CANCELED)))
            input.clear()
            input.onInputResumed()
            SystemClock.sleep(2)
            val l2 = key(input, KeyEvent.KEYCODE_BUTTON_L2, true)
            assertTrue(input.onKey(KeyEvent(l2.eventTime, l2.eventTime, KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)))
            key(input, KeyEvent.KEYCODE_BUTTON_L2, false)
            SystemClock.sleep(2)
            assertTrue(input.onKey(KeyEvent(l2.downTime, SystemClock.uptimeMillis(), KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_R2, 0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)))
            assertEquals("canceled UP before clear lost the held R2 guard", 1, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            SystemClock.sleep(2)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
            assertEquals(2, toggles)
            key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
            input.clear()
        }
    }

    @Test fun knownMixedTriggerWaitsForBothReleasesEvenBeforeItsFirstAxisSample() {
        instrumentation.runOnMainSync {
            for (digitalFirst in listOf(false, true)) {
                var toggles = 0
                val input = PhysicalInput(object : PhysicalInput.Callbacks {
                    override fun onPhysicalInput() {}
                    override fun onMenuKey() {}
                    override fun onToggleBottomScreen() {}
                    override fun onFastForwardToggle() { toggles++ }
                }, rightTriggerAxis = { true }).apply { fastForwardEnabled = true }
                val r2 = key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
                assertEquals(1, toggles)
                input.clear()
                fun releaseDigital() = input.observeKeyEvent(KeyEvent.changeAction(r2, KeyEvent.ACTION_UP), suspended = true)
                fun releaseAnalog() {
                    val now = SystemClock.uptimeMillis()
                    val event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
                        arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
                        arrayOf(MotionEvent.PointerCoords().apply { setAxisValue(MotionEvent.AXIS_RTRIGGER, 0f) }),
                        0, 0, 1f, 1f, 7, 0, InputDevice.SOURCE_JOYSTICK, 0)
                    try { input.observeMotionEvent(event) } finally { event.recycle() }
                }
                if (digitalFirst) releaseDigital() else releaseAnalog()
                assertEquals("one release channel toggled fast-forward", 1, toggles)
                if (digitalFirst) releaseAnalog() else {
                    input.onInputResumed()
                    // After analog neutral, a rise below the press threshold
                    // is not an analog hold just because digital UP is due.
                    axes(input, 0f, 0.48f)
                    key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
                }
                assertEquals("finishing release itself toggled fast-forward", 1, toggles)
                input.onInputResumed()
                key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
                axes(input, 0f, 1f)
                assertEquals(2, toggles)
                key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
                axes(input, 0f, 0f)
                input.clear()
            }
        }
    }

    @Test fun disablingFastForwardStillObservesExistingReleaseRequirements() {
        instrumentation.runOnMainSync {
            for (analog in listOf(false, true)) {
                var toggles = 0
                val input = PhysicalInput(object : PhysicalInput.Callbacks {
                    override fun onPhysicalInput() {}
                    override fun onMenuKey() {}
                    override fun onToggleBottomScreen() {}
                    override fun onFastForwardToggle() { toggles++ }
                }, rightTriggerAxis = { analog }).apply { fastForwardEnabled = true }
                key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
                if (analog) axes(input, 0f, 1f)
                assertEquals(1, toggles)
                input.clear()
                input.fastForwardEnabled = false
                key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
                if (analog) axes(input, 0f, 0f)
                assertEquals("release while disabled activated fast-forward", 1, toggles)
                assertEquals(0, InputHub.sentKeys)
                input.fastForwardEnabled = true
                key(input, KeyEvent.KEYCODE_BUTTON_R2, true)
                if (analog) axes(input, 0f, 1f)
                assertEquals("release while disabled was lost", 2, toggles)
                key(input, KeyEvent.KEYCODE_BUTTON_R2, false)
                if (analog) axes(input, 0f, 0f)
                input.clear()
            }
        }
    }
}
