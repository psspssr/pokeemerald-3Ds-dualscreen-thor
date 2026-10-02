package com.emerald3ds.android

import android.os.Looper
import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.preference.Preference
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.appbar.MaterialToolbar
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** UI lifecycle tests inject a backend; they never grant events to a real save. */
@RunWith(AndroidJUnit4::class)
class MysteryEventsTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private class FakeBackend(@Volatile var snapshot: IntArray) : MysteryEventsBackend {
        val reads = AtomicInteger()
        val readStarted = CountDownLatch(1)
        val activations = Collections.synchronizedList(mutableListOf<Int>())
        val started = CountDownLatch(1)
        @Volatile var finish: CountDownLatch? = null
        @Volatile var result = MysteryEventsModel.RESULT_ACTIVATED
        @Volatile var failRead = false
        @Volatile var readFinish: CountDownLatch? = null

        override fun read(): IntArray {
            check(Looper.myLooper() != Looper.getMainLooper())
            reads.incrementAndGet()
            readStarted.countDown()
            check(readFinish?.await(10, TimeUnit.SECONDS) != false)
            if (failRead) throw IllegalStateException("test unavailable")
            return snapshot.copyOf()
        }

        override fun activate(event: Int): Int {
            check(Looper.myLooper() != Looper.getMainLooper())
            activations.add(event)
            started.countDown()
            check(finish?.await(10, TimeUnit.SECONDS) != false)
            if (result == MysteryEventsModel.RESULT_ACTIVATED)
                snapshot = snapshot.copyOf().apply { this[event] = MysteryEventStatus.UNLOCKED.code }
            return result
        }
    }

    private fun launch(backend: FakeBackend): ActivityScenario<SettingsActivity> =
        ActivityScenario.launch(SettingsActivity::class.java).also { scenario ->
            scenario.onActivity { activity ->
                val fragment = MysteryEventsFragment().apply {
                    modelFactory = MysteryEventsModel.Factory(backend)
                }
                activity.supportFragmentManager.beginTransaction()
                    .replace(R.id.settings_container, fragment).commitNow()
            }
            settled(scenario)
        }

    private fun fragment(activity: SettingsActivity) =
        activity.supportFragmentManager.findFragmentById(R.id.settings_container) as MysteryEventsFragment

    private fun settled(scenario: ActivityScenario<SettingsActivity>) {
        val deadline = SystemClock.uptimeMillis() + 5000
        while (SystemClock.uptimeMillis() < deadline) {
            var done = false
            scenario.onActivity { done = fragment(it).model.state.value?.busy == false }
            if (done) return
            SystemClock.sleep(20)
        }
        fail("event request did not finish")
    }

    private fun nodes(text: String): List<AccessibilityNodeInfo> =
        instrumentation.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(text).orEmpty()

    private fun button(resource: Int, click: Boolean = false) {
        val label = context.getString(resource)
        val deadline = SystemClock.uptimeMillis() + 3000
        while (SystemClock.uptimeMillis() < deadline) {
            val button = nodes(label).firstOrNull { it.isClickable && it.text?.toString() == label }
            if (button != null) {
                if (click) assertTrue(button.performAction(AccessibilityNodeInfo.ACTION_CLICK))
                return
            }
            SystemClock.sleep(30)
        }
        fail("missing event dialog button: $label")
    }

    private fun open(scenario: ActivityScenario<SettingsActivity>, event: MysteryEvent) {
        scenario.onActivity {
            val row = fragment(it).findPreference<Preference>(event.key)!!
            assertTrue(row.onPreferenceClickListener!!.onPreferenceClick(row))
        }
        button(android.R.string.cancel)
    }

    @Test fun eventsShowDescriptionsAndOnlyAvailableRowsOfferActivation() {
        val codes = intArrayOf(0, 1, 2, 7, 5, 6, 8)
        val backend = FakeBackend(IntArray(MysteryEvent.catalogue.size) { codes[it % codes.size] })
        launch(backend).use { scenario ->
            scenario.onActivity { activity ->
                assertEquals(context.getString(R.string.mystery_events_title),
                    activity.findViewById<MaterialToolbar>(R.id.toolbar).title)
                for (event in MysteryEvent.catalogue) {
                    val row = fragment(activity).findPreference<Preference>(event.key)!!
                    assertFalse(row.isPersistent)
                    assertTrue(row.summary.toString().contains(context.getString(event.summary)))
                    if (codes[event.id % codes.size] == 7)
                        assertTrue(row.summary.toString().contains(context.getString(event.prerequisite)))
                    else assertTrue(row.summary.toString().contains(context.getString(
                        MysteryEventStatus.fromCode(codes[event.id % codes.size]).label)))
                }
            }
            for (event in MysteryEvent.catalogue) {
                open(scenario, event)
                assertTrue(nodes(context.getString(event.description)).isNotEmpty())
                if (codes[event.id % codes.size] == 0) button(R.string.mystery_events_activate)
                else assertFalse(nodes(context.getString(R.string.mystery_events_activate))
                    .any { it.isClickable && it.text?.toString() == context.getString(R.string.mystery_events_activate) })
                button(android.R.string.cancel, true)
                instrumentation.waitForIdleSync()
            }
            assertTrue(backend.activations.isEmpty())
            val preferences = PreferenceManager.getDefaultSharedPreferences(context)
            for (event in MysteryEvent.catalogue) assertFalse(preferences.contains(event.key))
        }
    }

    @Test fun oneActivationSurvivesRecreationWithoutBlockingOrRepeating() {
        val backend = FakeBackend(IntArray(MysteryEvent.catalogue.size) { 0 })
        val finish = CountDownLatch(1)
        backend.finish = finish
        launch(backend).use { scenario ->
            try {
                lateinit var original: MysteryEventsModel
                scenario.onActivity { original = fragment(it).model }
                open(scenario, MysteryEvent.catalogue.first())
                button(R.string.mystery_events_activate, true)
                assertTrue(backend.started.await(3, TimeUnit.SECONDS))
                scenario.onActivity {
                    val current = fragment(it)
                    assertTrue(current.model.state.value!!.busy)
                    assertFalse(current.findPreference<Preference>("mystery_events_refresh")!!.isEnabled)
                    current.model.activate(0)
                    current.model.activate(1)
                    current.model.refresh()
                }
                // The queued request leaves the UI responsive and its model
                // retains the fake backend across the recreated Fragment.
                scenario.recreate()
                scenario.onActivity {
                    assertSame(original, fragment(it).model)
                    assertTrue(fragment(it).model.state.value!!.busy)
                }
                finish.countDown()
                settled(scenario)
                assertEquals(listOf(0), backend.activations.toList())
                scenario.onActivity {
                    assertEquals(MysteryEventStatus.UNLOCKED, fragment(it).model.state.value!!.status(0))
                    fragment(it).model.activate(0)
                    fragment(it).model.activate(-1)
                    fragment(it).model.activate(999)
                }
                assertEquals(listOf(0), backend.activations.toList())
                assertEquals(2, backend.reads.get()) // initial read and one post-action refresh
            } finally {
                finish.countDown()
            }
        }
    }

    @Test fun unknownOrMissingNativeStatesStayReadOnlyAndRefreshCanRecover() {
        val backend = FakeBackend(intArrayOf(99, -42))
        launch(backend).use { scenario ->
            scenario.onActivity {
                for (event in MysteryEvent.catalogue) {
                    assertEquals(MysteryEventStatus.UNAVAILABLE, fragment(it).model.state.value!!.status(event.id))
                    fragment(it).model.activate(event.id)
                }
            }
            assertTrue(backend.activations.isEmpty())
            backend.failRead = true
            scenario.onActivity { fragment(it).model.refresh() }
            settled(scenario)
            scenario.onActivity { assertEquals(MysteryEventStatus.UNAVAILABLE, fragment(it).model.state.value!!.status(0)) }
            backend.failRead = false
            backend.snapshot = IntArray(MysteryEvent.catalogue.size) { 0 }
            scenario.onActivity {
                val refresh = fragment(it).findPreference<Preference>("mystery_events_refresh")!!
                refresh.onPreferenceClickListener!!.onPreferenceClick(refresh)
            }
            settled(scenario)
            scenario.onActivity { assertTrue(fragment(it).model.state.value!!.canActivate(0)) }
            backend.result = MysteryEventsModel.RESULT_TIMEOUT
            open(scenario, MysteryEvent.catalogue.first())
            button(R.string.mystery_events_activate, true)
            settled(scenario)
            assertEquals(listOf(0), backend.activations.toList())
            scenario.onActivity { fragment(it).model.refresh() }
            settled(scenario)
            assertEquals(listOf(0), backend.activations.toList()) // no retry after timeout or refresh
        }
    }

    @Test fun leavingCancelsQueuedActionButAllowsStartedActionToFinish() {
        val initial = IntArray(MysteryEvent.catalogue.size) { 0 }
        val departed = FakeBackend(initial.copyOf())
        val blockedRead = FakeBackend(initial.copyOf())
        val releaseRead = CountDownLatch(1)
        blockedRead.readFinish = releaseRead
        val afterQueue = FakeBackend(initial.copyOf())
        val scenario = launch(departed)
        try {
            instrumentation.runOnMainSync { MysteryEventsModel(blockedRead).refresh() }
            assertTrue(blockedRead.readStarted.await(3, TimeUnit.SECONDS))
            scenario.onActivity { fragment(it).model.activate(0) }
            scenario.close() // destroys the Fragment's ViewModel while its action is queued
            instrumentation.runOnMainSync { MysteryEventsModel(afterQueue).refresh() }
            releaseRead.countDown()
            // A later read proves the queued action's closure has been drained.
            assertTrue(afterQueue.readStarted.await(3, TimeUnit.SECONDS))
            assertTrue(departed.activations.isEmpty())
            assertEquals(1, departed.reads.get()) // only the initial screen read
        } finally {
            releaseRead.countDown()
            scenario.close()
        }

        val alreadyStarted = FakeBackend(initial.copyOf())
        val finishAction = CountDownLatch(1)
        alreadyStarted.finish = finishAction
        val finishedQueue = FakeBackend(initial.copyOf())
        val running = launch(alreadyStarted)
        try {
            running.onActivity { fragment(it).model.activate(0) }
            assertTrue(alreadyStarted.started.await(3, TimeUnit.SECONDS))
            running.close()
            instrumentation.runOnMainSync { MysteryEventsModel(finishedQueue).refresh() }
            finishAction.countDown()
            assertTrue(finishedQueue.readStarted.await(3, TimeUnit.SECONDS))
            assertEquals(listOf(0), alreadyStarted.activations.toList())
            assertEquals(MysteryEventStatus.UNLOCKED.code, alreadyStarted.snapshot[0])
        } finally {
            finishAction.countDown()
            running.close()
        }
    }
}
