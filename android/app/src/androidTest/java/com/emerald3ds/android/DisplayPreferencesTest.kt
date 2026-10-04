package com.emerald3ds.android

import androidx.preference.ListPreference
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
class DisplayPreferencesTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val preferences get() = PreferenceManager.getDefaultSharedPreferences(context)

    @Before fun setup() {
        check(BuildConfig.HOST_HARNESS)
        preferences.edit().clear().commit()
    }

    @After fun cleanup() { preferences.edit().clear().commit() }

    @Test fun freshInstallUsesSharpPixelsWithoutChangingFullPanelLayout() {
        val settings = AppSettings.load(context)
        assertFalse(settings.linearFilter)
        assertEquals(DualScaling.FILL, settings.dualScaling)
        assertFalse(settings.integerScaling)
    }

    @Test fun oldPreviewSmoothingChangesOnceAndKeepsOtherPreferences() {
        preferences.edit().putString("filter", "linear")
            .putString("dual_scaling", "fit").putBoolean("integer_scaling", true)
            .putString("qol_speed", "2").putBoolean("qol_shared_exp", true).commit()
        assertFalse(AppSettings.load(context).linearFilter)
        assertEquals("nearest", preferences.getString("filter", null))
        assertEquals("fit", preferences.getString("dual_scaling", null))
        assertTrue(preferences.getBoolean("integer_scaling", false))
        assertEquals("2", preferences.getString("qol_speed", null))
        assertTrue(preferences.getBoolean("qol_shared_exp", false))

        // A new explicit Smooth choice survives later activity loads.
        preferences.edit().putString("filter", "linear").commit()
        assertTrue(AppSettings.load(context).linearFilter)
        assertTrue(AppSettings.load(context).linearFilter)
    }

    @Test fun existingSharpChoiceIsRetained() {
        preferences.edit().putString("filter", "nearest").commit()
        assertFalse(AppSettings.load(context).linearFilter)
        assertEquals("nearest", preferences.getString("filter", null))
    }

    @Test fun settingsOpenedDirectlyShowsMigratedFilterAndRetainsLaterChoice() {
        preferences.edit().putString("filter", "linear").commit()
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                activity.supportFragmentManager.executePendingTransactions()
                val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                    as SettingsActivity.SettingsFragment
                val filter = checkNotNull(fragment.findPreference<ListPreference>("filter"))
                assertEquals("nearest", filter.value)
                filter.value = "linear"
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                activity.supportFragmentManager.executePendingTransactions()
                val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                    as SettingsActivity.SettingsFragment
                assertEquals("linear", fragment.findPreference<ListPreference>("filter")!!.value)
                assertTrue(AppSettings.load(context).linearFilter)
            }
        }
    }
}
