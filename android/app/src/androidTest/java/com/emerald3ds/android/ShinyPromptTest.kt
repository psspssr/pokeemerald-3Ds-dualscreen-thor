package com.emerald3ds.android

import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.FrameLayout
import androidx.lifecycle.Lifecycle
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class ShinyPromptTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    @Before fun defaults() {
        check(BuildConfig.HOST_HARNESS)
        PreferenceManager.getDefaultSharedPreferences(context).edit().clear()
            .putBoolean("dual_display", false).commit()
    }

    private fun ready(scenario: ActivityScenario<GameActivity>) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            var enabled = false
            scenario.onActivity {
                enabled = (it.findViewById<FrameLayout>(R.id.overlay_container).getChildAt(0) as ControlsOverlayView).inputEnabled
            }
            if (enabled) return
            SystemClock.sleep(40)
        }
        fail("game input did not become ready")
    }

    private fun promptButton(resource: Int, click: Boolean) {
        val label = context.getString(resource)
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            val nodes = instrumentation.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(label).orEmpty()
            val button = nodes.firstOrNull { it.isClickable && it.text?.toString() == label }
            if (button != null) {
                if (click) assertTrue(button.performAction(AccessibilityNodeInfo.ACTION_CLICK))
                return
            }
            SystemClock.sleep(40)
        }
        fail("missing shiny confirmation button: $label")
    }

    @Test fun nativePromptRespectsStayAndExplicitRun() {
        val worker = Executors.newSingleThreadExecutor()
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                for (allow in listOf(false, true)) {
                    val result = worker.submit<Boolean> { NativeBridge.testShinyFleeRoundTrip() }
                    promptButton(if (allow) R.string.shiny_flee_run else R.string.shiny_flee_stay, true)
                    assertEquals(allow, result.get(5, TimeUnit.SECONDS))
                    ready(scenario)
                }
            }
        } finally {
            NativeBridge.setState(NativeBridge.STATE_PAUSED)
            worker.shutdownNow()
        }
    }

    @Test fun leavingAppCancelsNativePromptAndAllowsNextRequest() {
        val worker = Executors.newSingleThreadExecutor()
        try {
            ActivityScenario.launch(GameActivity::class.java).use { scenario ->
                ready(scenario)
                val canceled = worker.submit<Boolean> { NativeBridge.testShinyFleeRoundTrip() }
                promptButton(R.string.shiny_flee_stay, false)
                scenario.moveToState(Lifecycle.State.CREATED)
                assertFalse(canceled.get(5, TimeUnit.SECONDS))
                scenario.moveToState(Lifecycle.State.RESUMED)
                ready(scenario)
                val next = worker.submit<Boolean> { NativeBridge.testShinyFleeRoundTrip() }
                promptButton(R.string.shiny_flee_run, true)
                assertTrue(next.get(5, TimeUnit.SECONDS))
            }
        } finally {
            NativeBridge.setState(NativeBridge.STATE_PAUSED)
            worker.shutdownNow()
        }
    }
}
