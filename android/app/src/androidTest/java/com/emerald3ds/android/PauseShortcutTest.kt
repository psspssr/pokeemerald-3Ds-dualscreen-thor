package com.emerald3ds.android

import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
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

@RunWith(AndroidJUnit4::class)
class PauseShortcutTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val context get() = inst.targetContext

    @Before fun prepare() {
        check(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(context).edit().clear()
            .putBoolean("dual_display", false).commit()
        inst.runOnMainSync { InputHub.clear() }
    }

    @After fun clearInput() = inst.runOnMainSync { InputHub.clear() }

    private class Owner(private val clearOnPause: Boolean = true) : PhysicalInput.Callbacks {
        val input = PhysicalInput(this)
        var pauses = 0
        var keysAtPause = 0
        var fastToggles = 0
        var fastHeld = false
        override fun onPhysicalInput() {}
        override fun onToggleBottomScreen() {}
        override fun onMenuKey() {
            pauses++
            keysAtPause = InputHub.sentKeys
            if (clearOnPause) input.clear()
        }
        override fun onFastForwardToggle() { fastToggles++ }
        override fun onFastForwardHold(held: Boolean) { fastHeld = held }
    }

    private fun down(code: Int, device: Int = 7): KeyEvent {
        // A real new press occurs after the resume boundary, rather than
        // using KeyEvent(action, code)'s zero/unspecified downTime.
        SystemClock.sleep(2)
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD)
    }

    private fun up(press: KeyEvent) = KeyEvent(press.downTime, SystemClock.uptimeMillis(),
        KeyEvent.ACTION_UP, press.keyCode, 0, 0, press.deviceId, 0, 0, press.source)

    private fun send(input: PhysicalInput, event: KeyEvent) { assertTrue(input.onKey(event)) }

    private fun keyAt(code: Int, time: Long, device: Int = 7, eventTime: Long = time) =
        KeyEvent(time, eventTime, KeyEvent.ACTION_DOWN, code, 0, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD)

    private fun resumeBoundary(input: PhysicalInput): Long {
        repeat(100) {
            val before = SystemClock.uptimeMillis()
            input.onInputResumed()
            if (SystemClock.uptimeMillis() == before) return before
        }
        error("could not observe a single-millisecond input boundary")
    }

    @Test fun bothOrdersAndMappingsPauseOnceBeforeReentrantClear() {
        inst.runOnMainSync {
            for (labels in listOf(false, true)) for (leftFirst in listOf(false, true)) {
                val owner = Owner()
                val input = owner.input.apply { labelMapping = labels }
                val first = down(if (leftFirst) KeyEvent.KEYCODE_BUTTON_L1 else KeyEvent.KEYCODE_BUTTON_R1)
                val second = down(if (leftFirst) KeyEvent.KEYCODE_BUTTON_R1 else KeyEvent.KEYCODE_BUTTON_L1)
                send(input, first)
                assertEquals(if (leftFirst) CtrKeys.L else CtrKeys.R, InputHub.sentKeys)
                send(input, second)
                assertEquals(1, owner.pauses)
                assertEquals(0, owner.keysAtPause and (CtrKeys.L or CtrKeys.R))
                assertEquals(0, InputHub.sentKeys)
                input.onInputResumed()
                repeat(3) {
                    send(input, KeyEvent(first)); send(input, KeyEvent(second))
                    send(input, KeyEvent.changeTimeRepeat(second, SystemClock.uptimeMillis(), 2))
                }
                assertEquals(1, owner.pauses)
                assertEquals(0, InputHub.sentKeys)
                send(input, up(first)); send(input, up(second))
                send(input, down(first.keyCode)); send(input, down(second.keyCode))
                assertEquals(2, owner.pauses)
                assertEquals(0, InputHub.sentKeys)
            }
        }
    }

    @Test fun chordLatchesEvenWhenOwnerDoesNotClearAndOneHeldShoulderCannotRearm() {
        inst.runOnMainSync {
            val owner = Owner(clearOnPause = false)
            val input = owner.input
            val left = down(KeyEvent.KEYCODE_BUTTON_L1)
            val right = down(KeyEvent.KEYCODE_BUTTON_R1)
            send(input, left); send(input, right)
            send(input, KeyEvent(left)); send(input, KeyEvent(right))
            assertEquals(1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            send(input, up(left))
            val freshLeft = down(KeyEvent.KEYCODE_BUTTON_L1)
            send(input, freshLeft)
            send(input, KeyEvent(right))
            assertEquals(1, owner.pauses)
            assertEquals(CtrKeys.L, InputHub.sentKeys)
            send(input, up(freshLeft)); send(input, up(right))
            send(input, down(KeyEvent.KEYCODE_BUTTON_R1)); send(input, down(KeyEvent.KEYCODE_BUTTON_L1))
            assertEquals(2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun controllersDoNotCombineAndRemovalClearsOnlyThatDevicesState() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val left = down(KeyEvent.KEYCODE_BUTTON_L1, 10)
            val right = down(KeyEvent.KEYCODE_BUTTON_R1, 20)
            send(input, left); send(input, right)
            assertEquals(0, owner.pauses)
            assertEquals(CtrKeys.L or CtrKeys.R, InputHub.sentKeys)
            input.removeDevice(10)
            assertEquals(CtrKeys.R, InputHub.sentKeys)
            send(input, down(KeyEvent.KEYCODE_BUTTON_R1, 10))
            assertEquals(0, owner.pauses)
            send(input, down(KeyEvent.KEYCODE_BUTTON_L1, 10))
            assertEquals(1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            input.removeDevice(10)
            input.onInputResumed()
            send(input, KeyEvent(right))
            assertEquals(0, InputHub.sentKeys)
            send(input, down(KeyEvent.KEYCODE_BUTTON_L1, 10))
            send(input, down(KeyEvent.KEYCODE_BUTTON_R1, 10))
            assertEquals(2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            input.removeDevice(20)
        }
    }

    @Test fun freshPressesRecoverWhenDialogConsumedReleasesWithoutAcceptingOldHolds() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val left = down(KeyEvent.KEYCODE_BUTTON_L1)
            val right = down(KeyEvent.KEYCODE_BUTTON_R1)
            send(input, left); send(input, right)
            input.onInputResumed()
            // No UP reached the game while its dialog was open.
            val freshLeft = down(KeyEvent.KEYCODE_BUTTON_L1)
            send(input, freshLeft); send(input, KeyEvent(right))
            assertEquals(1, owner.pauses)
            assertEquals(CtrKeys.L, InputHub.sentKeys)
            send(input, down(KeyEvent.KEYCODE_BUTTON_R1))
            assertEquals(2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            input.onInputResumed()
            send(input, down(KeyEvent.KEYCODE_BUTTON_R1))
            send(input, down(KeyEvent.KEYCODE_BUTTON_L1))
            assertEquals(3, owner.pauses)
        }
    }

    @Test fun hardwareRepeatsWithRebasedDownTimeCannotReopenPause() {
        inst.runOnMainSync {
            for (first in listOf(KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1)) {
                val second = if (first == KeyEvent.KEYCODE_BUTTON_L1) KeyEvent.KEYCODE_BUTTON_R1 else KeyEvent.KEYCODE_BUTTON_L1
                val owner = Owner()
                val input = owner.input
                send(input, down(first)); send(input, down(second))
                assertEquals(1, owner.pauses)
                input.onInputResumed()
                // Linux EV_KEY repeats can arrive with repeatCount=0 and a
                // downTime rebased by an unrelated key on the same controller.
                val l2 = down(KeyEvent.KEYCODE_BUTTON_L2)
                send(input, l2); send(input, up(l2))
                SystemClock.sleep(2)
                val later = SystemClock.uptimeMillis()
                repeat(3) {
                    send(input, keyAt(first, l2.downTime, eventTime = later))
                    send(input, keyAt(second, l2.downTime, eventTime = later))
                }
                assertEquals("rebased hardware repeat reopened pause", 1, owner.pauses)
                assertEquals(0, InputHub.sentKeys)
                // A release may still be missed while another Activity owns
                // input. Separate fresh presses must remain usable afterward.
                send(input, down(first)); send(input, down(second))
                assertEquals(2, owner.pauses)
                assertEquals(0, InputHub.sentKeys)
            }
        }
    }

    @Test fun sameTimestampOtherKeyCannotRearmBlockedShoulders() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            send(input, down(KeyEvent.KEYCODE_BUTTON_L1)); send(input, down(KeyEvent.KEYCODE_BUTTON_R1))
            input.onInputResumed()
            val l2 = down(KeyEvent.KEYCODE_BUTTON_L2)
            send(input, l2)
            // The unrelated DOWN and both held repeats can share a kernel
            // report/millisecond; downTime==eventTime alone is not enough.
            val left = keyAt(KeyEvent.KEYCODE_BUTTON_L1, l2.eventTime)
            val right = keyAt(KeyEvent.KEYCODE_BUTTON_R1, l2.eventTime)
            repeat(3) { send(input, left); send(input, right) }
            send(input, up(l2))
            assertEquals(1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)

            // The collision guard belongs to one controller, not every
            // device producing an event within the same millisecond.
            val otherDevice = down(KeyEvent.KEYCODE_BUTTON_L2, device = 8)
            input.observeKeyEvent(otherDevice)
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, otherDevice.eventTime))
            assertEquals(CtrKeys.L, InputHub.sentKeys)
            send(input, down(KeyEvent.KEYCODE_BUTTON_R1))
            assertEquals(2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun blockedShouldersAtResumeBoundaryRequireRelease() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            send(input, down(KeyEvent.KEYCODE_BUTTON_L1)); send(input, down(KeyEvent.KEYCODE_BUTTON_R1))
            SystemClock.sleep(2)
            val boundary = resumeBoundary(input)
            // A dialog-owned key may rebase a held shoulder exactly when
            // Resume enables input. Unknown freshness must stay blocked.
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, boundary))
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_R1, boundary))
            assertEquals(1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)

            val releasedLeft = keyAt(KeyEvent.KEYCODE_BUTTON_L1, boundary)
            val releasedRight = keyAt(KeyEvent.KEYCODE_BUTTON_R1, boundary)
            input.observeKeyEvent(up(releasedLeft)); input.observeKeyEvent(up(releasedRight))
            assertEquals(0, InputHub.sentKeys)
            val freshTime = down(KeyEvent.KEYCODE_BUTTON_L1).eventTime
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, freshTime))
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_R1, freshTime))
            assertEquals("observed releases should permit simultaneous new shoulders", 2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun shouldersFirstPressedInDialogStayBlockedAcrossRebasedRepeats() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            send(input, down(KeyEvent.KEYCODE_ESCAPE))
            assertEquals(1, owner.pauses)
            val left = down(KeyEvent.KEYCODE_BUTTON_L1)
            val right = down(KeyEvent.KEYCODE_BUTTON_R1)
            input.observeKeyEvent(left, suspended = true)
            input.observeKeyEvent(right, suspended = true)
            assertEquals(0, InputHub.sentKeys)
            input.onInputResumed()
            val l2 = down(KeyEvent.KEYCODE_BUTTON_L2)
            send(input, l2)
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, l2.eventTime))
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_R1, l2.eventTime))
            send(input, up(l2))
            assertEquals("dialog-owned shoulder presses were not quarantined", 1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            input.observeKeyEvent(up(left)); input.observeKeyEvent(up(right))
            val freshTime = down(KeyEvent.KEYCODE_BUTTON_L1).eventTime
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, freshTime))
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_R1, freshTime))
            assertEquals(2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun canceledShoulderUpsDoNotCountAsPhysicalReleases() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val left = down(KeyEvent.KEYCODE_BUTTON_L1)
            send(input, left)
            // Focus cancellation can precede clear(), before any pause guard
            // existed. It releases game input without proving physical UP.
            send(input, KeyEvent.changeFlags(up(left), KeyEvent.FLAG_CANCELED))
            assertEquals(0, InputHub.sentKeys)
            send(input, down(KeyEvent.KEYCODE_ESCAPE))
            val right = down(KeyEvent.KEYCODE_BUTTON_R1)
            input.observeKeyEvent(right, suspended = true)
            input.observeKeyEvent(KeyEvent.changeFlags(up(right), KeyEvent.FLAG_CANCELED), suspended = true)
            input.onInputResumed()
            val l2 = down(KeyEvent.KEYCODE_BUTTON_L2)
            send(input, l2)
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, l2.eventTime))
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_R1, l2.eventTime))
            send(input, up(l2))
            assertEquals("focus cancellation rearmed physical shoulders", 1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            send(input, up(left)); send(input, up(right))
            val freshTime = down(KeyEvent.KEYCODE_BUTTON_L1).eventTime
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_L1, freshTime))
            send(input, keyAt(KeyEvent.KEYCODE_BUTTON_R1, freshTime))
            assertEquals(2, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun resumeRejectsShouldersFirstPressedWhileTheGameWasSuspended() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            input.clear()
            val hiddenLeft = down(KeyEvent.KEYCODE_BUTTON_L1)
            val hiddenRight = down(KeyEvent.KEYCODE_BUTTON_R1)
            SystemClock.sleep(2)
            input.onInputResumed()
            send(input, hiddenLeft); send(input, hiddenRight)
            assertEquals(0, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
            send(input, up(hiddenLeft)); send(input, up(hiddenRight))
            send(input, down(KeyEvent.KEYCODE_BUTTON_L1)); send(input, down(KeyEvent.KEYCODE_BUTTON_R1))
            assertEquals(1, owner.pauses)
        }
    }

    @Test fun freshDownsInTheSameMillisecondAsResumeStillWorkButHeldReplaysDoNot() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input
            val time = resumeBoundary(input)
            val left = KeyEvent(time, time, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_L1,
                0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)
            val right = KeyEvent(time, time, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_R1,
                0, 0, 7, 0, 0, InputDevice.SOURCE_GAMEPAD)
            send(input, left)
            assertEquals(CtrKeys.L, InputHub.sentKeys)
            send(input, right)
            assertEquals(1, owner.pauses)
            input.onInputResumed()
            send(input, KeyEvent(left)); send(input, KeyEvent(right))
            assertEquals(1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
        }
    }

    @Test fun singleShouldersFastTriggersKeyboardAndVirtualButtonsKeepTheirBindings() {
        inst.runOnMainSync {
            val owner = Owner()
            val input = owner.input.apply { fastForwardEnabled = true }
            for ((code, expected) in listOf(KeyEvent.KEYCODE_BUTTON_L1 to CtrKeys.L, KeyEvent.KEYCODE_BUTTON_R1 to CtrKeys.R)) {
                val key = down(code)
                send(input, key); assertEquals(expected, InputHub.sentKeys)
                send(input, up(key)); assertEquals(0, InputHub.sentKeys)
            }
            val l2 = down(KeyEvent.KEYCODE_BUTTON_L2)
            val r2 = down(KeyEvent.KEYCODE_BUTTON_R2)
            send(input, l2); send(input, r2)
            assertTrue(owner.fastHeld)
            assertEquals(1, owner.fastToggles)
            assertEquals(0, owner.pauses)
            send(input, up(l2)); send(input, up(r2))
            assertFalse(owner.fastHeld)
            val q = down(KeyEvent.KEYCODE_Q)
            val w = down(KeyEvent.KEYCODE_W)
            send(input, q); send(input, w)
            assertEquals(CtrKeys.L or CtrKeys.R, InputHub.sentKeys)
            assertEquals(0, owner.pauses)
            send(input, up(q)); send(input, up(w))
            InputHub.setKeys(InputHub.SRC_OVERLAY, CtrKeys.L or CtrKeys.R)
            assertEquals(CtrKeys.L or CtrKeys.R, InputHub.sentKeys)
            assertEquals(0, owner.pauses)
            InputHub.setKeys(InputHub.SRC_OVERLAY, 0)
            send(input, down(KeyEvent.KEYCODE_ESCAPE))
            assertEquals(1, owner.pauses)
            assertEquals(0, InputHub.sentKeys)
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

    private fun closeMenu(scenario: ActivityScenario<GameActivity>, second: Boolean = false) {
        val back = down(KeyEvent.KEYCODE_BACK)
        scenario.onActivity {
            if (second) {
                it.presentation!!.dispatchKeyEvent(back)
                it.presentation!!.dispatchKeyEvent(up(back))
            } else {
                it.dispatchKeyEvent(back)
                it.dispatchKeyEvent(up(back))
            }
        }
        ready(scenario)
    }

    @Test fun dialogWindowObservesShoulderReleasesAndHiddenPresses() {
        val field = GameActivity::class.java.getDeclaredField("menuDialog").apply { isAccessible = true }
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val left = down(KeyEvent.KEYCODE_BUTTON_L1, 904)
            val right = down(KeyEvent.KEYCODE_BUTTON_R1, 904)
            scenario.onActivity { it.dispatchKeyEvent(left); it.dispatchKeyEvent(right) }
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            scenario.onActivity { activity ->
                // WindowManager delivers focused-dialog keys directly to
                // Dialog.dispatchKeyEvent, bypassing Activity.dispatchKeyEvent.
                val dialog = field.get(activity) as AlertDialog
                dialog.dispatchKeyEvent(up(left)); dialog.dispatchKeyEvent(up(right))
            }
            closeMenu(scenario)
            val now = down(KeyEvent.KEYCODE_BUTTON_L1, 904).eventTime
            scenario.onActivity {
                it.dispatchKeyEvent(keyAt(KeyEvent.KEYCODE_BUTTON_L1, now, device = 904))
                it.dispatchKeyEvent(keyAt(KeyEvent.KEYCODE_BUTTON_R1, now, device = 904))
            }
            assertEquals("dialog consumed releases without rearming the chord", NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            assertEquals(0, HostProbe.snapshot()[1])
            closeMenu(scenario)

            scenario.onActivity { it.dispatchKeyEvent(down(KeyEvent.KEYCODE_ESCAPE)) }
            val hiddenLeft = down(KeyEvent.KEYCODE_BUTTON_L1, 905)
            val hiddenRight = down(KeyEvent.KEYCODE_BUTTON_R1, 905)
            scenario.onActivity { activity ->
                val dialog = field.get(activity) as AlertDialog
                dialog.dispatchKeyEvent(hiddenLeft); dialog.dispatchKeyEvent(hiddenRight)
            }
            closeMenu(scenario)
            val l2 = down(KeyEvent.KEYCODE_BUTTON_L2, 905)
            scenario.onActivity {
                it.dispatchKeyEvent(l2)
                it.dispatchKeyEvent(keyAt(KeyEvent.KEYCODE_BUTTON_L1, l2.eventTime, device = 905))
                it.dispatchKeyEvent(keyAt(KeyEvent.KEYCODE_BUTTON_R1, l2.eventTime, device = 905))
                it.dispatchKeyEvent(up(l2))
            }
            assertEquals("held shoulders first pressed in a dialog reopened it", NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
            assertEquals(0, HostProbe.snapshot()[1])
            scenario.onActivity { it.dispatchKeyEvent(up(hiddenLeft)); it.dispatchKeyEvent(up(hiddenRight)) }
            val freshTime = down(KeyEvent.KEYCODE_BUTTON_L1, 905).eventTime
            scenario.onActivity {
                it.dispatchKeyEvent(keyAt(KeyEvent.KEYCODE_BUTTON_L1, freshTime, device = 905))
                it.dispatchKeyEvent(keyAt(KeyEvent.KEYCODE_BUTTON_R1, freshTime, device = 905))
            }
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            closeMenu(scenario)
        }
    }

    @Test fun mainWindowRoutesChordAndClearsReplaysAcrossMenuAndActivityResume() {
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            ready(scenario)
            val left = down(KeyEvent.KEYCODE_BUTTON_L1, 901)
            val right = down(KeyEvent.KEYCODE_BUTTON_R1, 901)
            scenario.onActivity { it.dispatchKeyEvent(left) }
            assertEquals(CtrKeys.L, HostProbe.snapshot()[1])
            scenario.onActivity { it.dispatchKeyEvent(right) }
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            assertEquals(0, HostProbe.snapshot()[1])
            scenario.onActivity {
                it.dispatchKeyEvent(up(left)); it.dispatchKeyEvent(up(right))
            }
            closeMenu(scenario)
            scenario.onActivity {
                it.dispatchKeyEvent(KeyEvent(left)); it.dispatchKeyEvent(KeyEvent(right))
                it.dispatchKeyEvent(KeyEvent.changeTimeRepeat(right, SystemClock.uptimeMillis(), 2))
            }
            assertEquals(NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
            assertEquals(0, HostProbe.snapshot()[1])
            val freshLeft = down(KeyEvent.KEYCODE_BUTTON_L1, 901)
            val freshRight = down(KeyEvent.KEYCODE_BUTTON_R1, 901)
            scenario.onActivity { it.dispatchKeyEvent(freshLeft); it.dispatchKeyEvent(freshRight) }
            assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
            closeMenu(scenario)
            scenario.moveToState(Lifecycle.State.CREATED)
            val hiddenLeft = down(KeyEvent.KEYCODE_BUTTON_L1, 902)
            val hiddenRight = down(KeyEvent.KEYCODE_BUTTON_R1, 902)
            scenario.moveToState(Lifecycle.State.RESUMED)
            ready(scenario)
            scenario.onActivity { it.dispatchKeyEvent(hiddenLeft); it.dispatchKeyEvent(hiddenRight) }
            assertEquals(NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
            assertEquals(0, HostProbe.snapshot()[1])
            val ordinary = down(KeyEvent.KEYCODE_BUTTON_R1, 902)
            scenario.onActivity { it.dispatchKeyEvent(ordinary) }
            assertEquals(CtrKeys.R, HostProbe.snapshot()[1])
            scenario.onActivity { it.dispatchKeyEvent(up(ordinary)) }
            assertEquals(0, HostProbe.snapshot()[1])
        }
    }

    @Test fun presentationAndMainShareOneChordOwnerAndExistingMenuRouting() {
        PreferenceManager.getDefaultSharedPreferences(context).edit().putBoolean("dual_display", true).commit()
        var display: VirtualDisplay? = null
        var reader: ImageReader? = null
        inst.runOnMainSync {
            reader = ImageReader.newInstance(1240, 1080, PixelFormat.RGBA_8888, 3).apply {
                setOnImageAvailableListener({ it.acquireLatestImage()?.close() }, Handler(Looper.getMainLooper()))
            }
            display = context.getSystemService(DisplayManager::class.java).createVirtualDisplay(
                "Pause shortcut bottom display", 1240, 1080, 240, reader!!.surface,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC or DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION or
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY,
            )
            assertNotNull(display)
        }
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val deadline = SystemClock.uptimeMillis() + 10_000
                var secondReady = false
                while (!secondReady && SystemClock.uptimeMillis() < deadline) {
                    scenario.onActivity { secondReady = it.presentation?.surfaceWidth == 1240 }
                    if (!secondReady) SystemClock.sleep(30)
                }
                assertTrue(secondReady)
                val right = down(KeyEvent.KEYCODE_BUTTON_R1, 903)
                val left = down(KeyEvent.KEYCODE_BUTTON_L1, 903)
                scenario.onActivity { it.presentation!!.dispatchKeyEvent(right) }
                assertEquals(CtrKeys.R, HostProbe.snapshot()[1])
                scenario.onActivity { it.dispatchKeyEvent(left) }
                assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                assertEquals(0, HostProbe.snapshot()[1])
                scenario.onActivity {
                    it.presentation!!.dispatchKeyEvent(up(right)); it.presentation!!.dispatchKeyEvent(up(left))
                }
                closeMenu(scenario, second = true)
                scenario.onActivity {
                    it.presentation!!.dispatchKeyEvent(KeyEvent(right)); it.dispatchKeyEvent(KeyEvent(left))
                }
                assertEquals(NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
                assertEquals(0, HostProbe.snapshot()[1])
                val freshLeft = down(KeyEvent.KEYCODE_BUTTON_L1, 903)
                val freshRight = down(KeyEvent.KEYCODE_BUTTON_R1, 903)
                scenario.onActivity { it.dispatchKeyEvent(freshLeft); it.presentation!!.dispatchKeyEvent(freshRight) }
                assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                closeMenu(scenario, second = true)
                val escape = down(KeyEvent.KEYCODE_ESCAPE)
                scenario.onActivity { it.presentation!!.dispatchKeyEvent(escape) }
                assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                closeMenu(scenario, second = true)
            }
        } finally {
            inst.runOnMainSync { display?.release(); reader?.close() }
        }
    }
}
