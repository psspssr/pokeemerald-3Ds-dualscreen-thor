package com.emerald3ds.android

import android.os.SystemClock
import android.graphics.Rect
import android.view.InputDevice
import android.view.MotionEvent
import androidx.appcompat.app.AlertDialog
import androidx.lifecycle.Lifecycle
import androidx.preference.ListPreference
import androidx.preference.ListPreferenceDialogFragmentCompat
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
class VoxelAntiAliasingTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val context get() = inst.targetContext
    private val preferences get() = PreferenceManager.getDefaultSharedPreferences(context)

    @Before fun setup() {
        check(BuildConfig.HOST_HARNESS)
        preferences.edit().clear().putBoolean("dual_display", false).commit()
        HostProbe.setVoxelAACapabilities(-1)
    }

    @After fun cleanup() { HostProbe.setVoxelAACapabilities(-1) }

    private fun fragment(activity: SettingsActivity): SettingsActivity.SettingsFragment {
        activity.supportFragmentManager.executePendingTransactions()
        return activity.supportFragmentManager.findFragmentById(R.id.settings_container)
            as SettingsActivity.SettingsFragment
    }

    private fun choose(scenario: ActivityScenario<SettingsActivity>, index: Int) {
        scenario.onActivity { activity ->
            val settings = fragment(activity)
            settings.onDisplayPreferenceDialog(settings.findPreference<ListPreference>("voxel_aa")!!)
            activity.supportFragmentManager.executePendingTransactions()
        }
        inst.waitForIdleSync()
        var x = 0f
        var y = 0f
        var expected = ""
        scenario.onActivity { activity ->
            val dialog = activity.supportFragmentManager.fragments.filterIsInstance<ListPreferenceDialogFragmentCompat>().single()
            val list = (dialog.requireDialog() as AlertDialog).listView
            val bounds = Rect()
            // A dialog message can hide the list while its adapter and
            // performItemClick still work. Require an actual visible row.
            assertTrue("AA choices are hidden behind dialog content", list.isShown && list.getGlobalVisibleRect(bounds))
            val row = checkNotNull(list.getChildAt(index - list.firstVisiblePosition))
            assertTrue("AA choice is not visible", row.isShown && row.getGlobalVisibleRect(bounds))
            val location = IntArray(2)
            row.getLocationOnScreen(location)
            x = location[0] + row.width / 2f
            y = location[1] + row.height / 2f
            expected = fragment(activity).findPreference<ListPreference>("voxel_aa")!!.entryValues[index].toString()
        }
        val downTime = SystemClock.uptimeMillis()
        val pointer = MotionEvent.PointerProperties().apply { id = 0; toolType = MotionEvent.TOOL_TYPE_FINGER }
        val coordinates = MotionEvent.PointerCoords().apply {
            this.x = x
            this.y = y
            pressure = 1f
            size = 1f
        }
        for (action in listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP)) {
            if (action == MotionEvent.ACTION_UP) SystemClock.sleep(100)
            val event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, 1,
                arrayOf(pointer), arrayOf(coordinates), 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
            try { assertTrue(inst.uiAutomation.injectInputEvent(event, true)) } finally { event.recycle() }
        }
        // AbsListView may post its click after the pressed-state delay. An
        // idle looper can still have that timed callback waiting, so do not
        // recreate the activity until both its effects are observable.
        val deadline = SystemClock.uptimeMillis() + 5_000
        var selected: String? = null
        var dismissed = false
        while (SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity { activity ->
                selected = fragment(activity).findPreference<ListPreference>("voxel_aa")!!.value
                dismissed = activity.supportFragmentManager.fragments
                    .filterIsInstance<ListPreferenceDialogFragmentCompat>().none { it.dialog?.isShowing == true }
            }
            if (selected == expected && preferences.getString("voxel_aa", null) == expected && dismissed) return
            SystemClock.sleep(25)
        }
        fail("Visible AA tap did not settle: expected=$expected selected=$selected dismissed=$dismissed")
    }

    private fun assertSummary(quality: String, preference: ListPreference) {
        assertEquals("$quality\n${context.getString(R.string.pref_voxel_aa_description)}", preference.summary)
    }

    private fun waitForRequest(samples: Int) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            val state = HostProbe.snapshot()
            if (state[19] == samples && state[16] > 0) return
            SystemClock.sleep(25)
        }
        fail("AA request $samples did not reach the active native layout")
    }

    @Test fun defaultIsOffAndUnavailableRequestsAreNotRewritten() {
        assertEquals(0, AppSettings.load(context).voxelAASamples)
        for (samples in listOf(0, 2, 4)) {
            preferences.edit().putString("voxel_aa", samples.toString()).commit()
            assertEquals(samples, AppSettings.load(context).voxelAASamples)
        }
        HostProbe.setVoxelAACapabilities(1)
        assertEquals(4, AppSettings.load(context).voxelAASamples)
        assertEquals(2, VoxelAntiAliasing.effective(4, 1))
        assertEquals(0, VoxelAntiAliasing.effective(2, 2)) // Never silently increase a 2x request to 4x.
        for (invalid in listOf("1", "3", "8", "-1", "invalid")) {
            preferences.edit().putString("voxel_aa", invalid).commit()
            assertEquals(0, AppSettings.load(context).voxelAASamples)
        }
    }

    @Test fun unsupportedChoicesAreOmittedAndSavedQualityRemainsHonest() {
        preferences.edit().putString("voxel_aa", "4").commit()
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertSummary(context.getString(R.string.pref_voxel_aa_unknown, "4×"), pref)
                assertEquals("4", pref.value)
            }
            HostProbe.setVoxelAACapabilities(1)
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0", "2"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertSummary(context.getString(R.string.pref_voxel_aa_unavailable, "4×", "2×"), pref)
                assertEquals("4", preferences.getString("voxel_aa", null))
            }
            HostProbe.setVoxelAACapabilities(2)
            preferences.edit().putString("voxel_aa", "2").commit()
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0", "4"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertSummary(context.getString(R.string.pref_voxel_aa_unavailable, "2×", "Off"), pref)
            }
            HostProbe.setVoxelAACapabilities(0)
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertEquals("2", pref.value)
            }
            choose(scenario, 0) // Off is always available, even on an unsupported device.
            inst.waitForIdleSync()
            assertEquals("0", preferences.getString("voxel_aa", null))
        }
    }

    @Test fun chosenQualityPersistsAndReachesNativeAcrossPauseAndResume() {
        HostProbe.setVoxelAACapabilities(3)
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            for ((index, samples) in listOf(2 to 4, 1 to 2)) {
                choose(scenario, index)
                scenario.recreate()
                scenario.onActivity { activity ->
                    val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                    assertEquals(samples.toString(), pref.value)
                    assertSummary("$samples×", pref)
                    assertEquals(samples, AppSettings.load(context).voxelAASamples)
                }
            }
        }
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitForRequest(2)
            scenario.moveToState(Lifecycle.State.CREATED)
            preferences.edit().putString("voxel_aa", "4").commit()
            HostProbe.setVoxelAACapabilities(1)
            scenario.moveToState(Lifecycle.State.RESUMED)
            waitForRequest(4) // Renderer can fall back; Android retains the requested quality.
            assertEquals("4", preferences.getString("voxel_aa", null))
        }
    }
}
