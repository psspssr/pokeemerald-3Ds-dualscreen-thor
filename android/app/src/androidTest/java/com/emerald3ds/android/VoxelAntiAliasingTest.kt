package com.emerald3ds.android

import android.os.SystemClock
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

    private fun choose(activity: SettingsActivity, index: Int) {
        val settings = fragment(activity)
        settings.onDisplayPreferenceDialog(settings.findPreference<ListPreference>("voxel_aa")!!)
        activity.supportFragmentManager.executePendingTransactions()
        val dialog = activity.supportFragmentManager.fragments.filterIsInstance<ListPreferenceDialogFragmentCompat>().single()
        val list = (dialog.requireDialog() as AlertDialog).listView
        assertTrue(list.performItemClick(null, index, list.adapter.getItemId(index)))
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
                assertEquals(context.getString(R.string.pref_voxel_aa_unknown, "4×"), pref.summary)
                assertEquals("4", pref.value)
            }
            HostProbe.setVoxelAACapabilities(1)
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0", "2"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertEquals(context.getString(R.string.pref_voxel_aa_unavailable, "4×", "2×"), pref.summary)
                assertEquals("4", preferences.getString("voxel_aa", null))
            }
            HostProbe.setVoxelAACapabilities(2)
            preferences.edit().putString("voxel_aa", "2").commit()
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0", "4"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertEquals(context.getString(R.string.pref_voxel_aa_unavailable, "2×", "Off"), pref.summary)
            }
            HostProbe.setVoxelAACapabilities(0)
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertArrayEquals(arrayOf("0"), pref.entryValues.map { it.toString() }.toTypedArray())
                assertEquals("2", pref.value)
                choose(activity, 0) // Off is always available, even on an unsupported device.
            }
            inst.waitForIdleSync()
            assertEquals("0", preferences.getString("voxel_aa", null))
        }
    }

    @Test fun chosenQualityPersistsAndReachesNativeAcrossPauseAndResume() {
        HostProbe.setVoxelAACapabilities(3)
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> choose(activity, 1) }
            inst.waitForIdleSync()
            scenario.recreate()
            scenario.onActivity { activity ->
                val pref = fragment(activity).findPreference<ListPreference>("voxel_aa")!!
                assertEquals("2", pref.value)
                assertEquals("2×", pref.summary)
                assertEquals(2, AppSettings.load(context).voxelAASamples)
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
