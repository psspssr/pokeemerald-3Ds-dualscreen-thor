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
class VoxelResolutionTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val context get() = inst.targetContext
    private val preferences get() = PreferenceManager.getDefaultSharedPreferences(context)

    @Before fun setup() {
        check(BuildConfig.HOST_HARNESS)
        preferences.edit().clear().putBoolean("dual_display", false).commit()
        HostProbe.setVoxelScaleCapabilities(-1)
    }

    @After fun cleanup() { HostProbe.setVoxelScaleCapabilities(-1) }

    private fun fragment(activity: SettingsActivity): SettingsActivity.SettingsFragment {
        activity.supportFragmentManager.executePendingTransactions()
        return activity.supportFragmentManager.findFragmentById(R.id.settings_container)
            as SettingsActivity.SettingsFragment
    }

    private fun choose(scenario: ActivityScenario<SettingsActivity>, index: Int) {
        scenario.onActivity { activity ->
            val settings = fragment(activity)
            settings.onDisplayPreferenceDialog(settings.findPreference<ListPreference>("voxel_resolution")!!)
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
            assertTrue("Resolution choices are hidden behind dialog content", list.isShown && list.getGlobalVisibleRect(bounds))
            val row = checkNotNull(list.getChildAt(index - list.firstVisiblePosition))
            assertTrue("Resolution choice is not visible", row.isShown && row.getGlobalVisibleRect(bounds))
            val location = IntArray(2)
            row.getLocationOnScreen(location)
            x = location[0] + row.width / 2f
            y = location[1] + row.height / 2f
            expected = fragment(activity).findPreference<ListPreference>("voxel_resolution")!!.entryValues[index].toString()
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
                selected = fragment(activity).findPreference<ListPreference>("voxel_resolution")!!.value
                dismissed = activity.supportFragmentManager.fragments
                    .filterIsInstance<ListPreferenceDialogFragmentCompat>().none { it.dialog?.isShowing == true }
            }
            if (selected == expected && preferences.getString("voxel_resolution", null) == expected && dismissed) return
            SystemClock.sleep(25)
        }
        fail("Visible resolution tap did not settle: expected=$expected selected=$selected dismissed=$dismissed")
    }

    private fun waitForRequest(scale: Int) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            val state = HostProbe.snapshot()
            if (state[20] == scale && state[16] > 0) return
            SystemClock.sleep(25)
        }
        fail("Resolution request $scale did not reach the native layout")
    }

    @Test fun originalDefaultAndResourceFallbackPreserveStoredRequest() {
        assertEquals(1, AppSettings.load(context).voxelScale)
        preferences.edit().putString("voxel_resolution", "4").commit()
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_resolution")!!
                assertArrayEquals(arrayOf("1"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertTrue(pref.summary.toString().contains("support is checked"))
                assertEquals("4", pref.value)
            }
            HostProbe.setVoxelScaleCapabilities(2)
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_resolution")!!
                assertArrayEquals(arrayOf("1", "2"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertTrue(pref.summary.toString().contains("unavailable; using 2×"))
                assertEquals("4", pref.value)
                assertEquals(4, AppSettings.load(context).voxelScale)
            }
            choose(scenario, 0)
            assertEquals("1", preferences.getString("voxel_resolution", null))
        }
    }

    @Test fun visibleChoicesPersistAndReachNativeAcrossPauseAndResume() {
        HostProbe.setVoxelScaleCapabilities(4)
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            for (scale in listOf(4, 3, 2, 1)) {
                choose(scenario, scale - 1)
                scenario.recreate()
                assertEquals(scale, AppSettings.load(context).voxelScale)
            }
        }
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            waitForRequest(1)
            scenario.moveToState(Lifecycle.State.CREATED)
            preferences.edit().putString("voxel_resolution", "4").commit()
            HostProbe.setVoxelScaleCapabilities(2)
            scenario.moveToState(Lifecycle.State.RESUMED)
            waitForRequest(4) // GPU may lower allocation; Android retains the requested setting.
            assertEquals("4", preferences.getString("voxel_resolution", null))
        }
    }
}
