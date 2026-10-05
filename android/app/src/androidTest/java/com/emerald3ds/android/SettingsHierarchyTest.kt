package com.emerald3ds.android

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import android.os.Looper
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.accessibility.AccessibilityNodeInfo
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceGroup
import androidx.preference.PreferenceManager
import androidx.preference.PreferenceScreen
import androidx.preference.SwitchPreferenceCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.appbar.MaterialToolbar
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SettingsHierarchyTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val context get() = inst.targetContext
    private val preferences get() = PreferenceManager.getDefaultSharedPreferences(context)

    @Before fun setup() {
        check(BuildConfig.HOST_HARNESS)
        preferences.edit().clear().putBoolean("sharp_filter_defaults_applied", true).commit()
    }

    @After fun cleanup() { preferences.edit().clear().commit() }

    private fun fragment(activity: SettingsActivity) =
        activity.supportFragmentManager.findFragmentById(R.id.settings_container) as PreferenceFragmentCompat

    private fun children(group: PreferenceGroup) = (0 until group.preferenceCount).map(group::getPreference)
    private fun descendants(group: PreferenceGroup): List<Preference> = children(group).flatMap {
        listOf(it) + if (it is PreferenceGroup) descendants(it) else emptyList()
    }

    private fun waitFor(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            if (condition()) return
            SystemClock.sleep(25)
        }
        fail(message)
    }

    private fun ready(scenario: ActivityScenario<SettingsActivity>, key: String?) = waitFor("Settings page $key did not attach") {
        var ready = false
        scenario.onActivity { activity ->
            activity.supportFragmentManager.executePendingTransactions()
            val current = fragment(activity)
            ready = current.preferenceScreen.key == key && current.view != null &&
                current.listView.childCount > 0 && activity.window.decorView.hasWindowFocus()
        }
        ready
    }

    private fun open(scenario: ActivityScenario<SettingsActivity>, key: String) {
        scenario.onActivity { activity ->
            val screen = fragment(activity).findPreference<PreferenceScreen>(key)!!
            screen.performClick()
            activity.supportFragmentManager.executePendingTransactions()
        }
        ready(scenario, key)
    }

    private fun back(scenario: ActivityScenario<SettingsActivity>, key: String?) {
        scenario.onActivity { it.onBackPressedDispatcher.onBackPressed() }
        ready(scenario, key)
    }

    private fun key(scenario: ActivityScenario<SettingsActivity>, code: Int) {
        scenario.onActivity { activity ->
            val now = SystemClock.uptimeMillis()
            for (action in listOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP)) {
                val event = KeyEvent(now, now + action, action, code, 0, 0, 445, 0,
                    KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_KEYBOARD)
                activity.window.callback.dispatchKeyEvent(event)
            }
        }
        inst.waitForIdleSync()
    }

    private fun touchToolbar(scenario: ActivityScenario<SettingsActivity>) {
        scenario.onActivity { activity ->
            val toolbar = activity.findViewById<MaterialToolbar>(R.id.toolbar)
            val position = IntArray(2).also(toolbar::getLocationInWindow)
            val now = SystemClock.uptimeMillis()
            for (action in listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP)) {
                val event = MotionEvent.obtain(now, now + action, action,
                    position[0] + toolbar.width - 8f, position[1] + toolbar.height / 2f, 0)
                event.source = InputDevice.SOURCE_TOUCHSCREEN
                try { activity.window.callback.dispatchTouchEvent(event) } finally { event.recycle() }
            }
        }
        inst.waitForIdleSync()
        assertFalse(MenuNavigation.usingController)
    }

    private fun focusedKey(activity: SettingsActivity): String? {
        val current = fragment(activity)
        val list = current.listView
        val focused = list.findFocus() ?: return null
        val child = list.findContainingItemView(focused) ?: return null
        val position = list.getChildAdapterPosition(child)
        val positions = list.adapter as PreferenceGroup.PreferencePositionCallback
        return descendants(current.preferenceScreen)
            .firstOrNull { positions.getPreferenceAdapterPosition(it) == position }?.key
    }

    private fun focus(scenario: ActivityScenario<SettingsActivity>, key: String) = waitFor("Expected focused preference $key") {
        var actual: String? = null
        scenario.onActivity { actual = focusedKey(it) }
        actual == key
    }

    @Test fun fiveRootDestinationsKeepEveryExistingSettingReachable() {
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            ready(scenario, null)
            scenario.onActivity { activity ->
                val root = fragment(activity).preferenceScreen
                assertEquals(listOf("display", "controls", "gameplay", "saves_data", "about"), children(root).map { it.key })
                assertTrue(children(root).all { it.isEnabled && it.isSelectable })
                assertFalse(root.findPreference<Preference>("about") is PreferenceScreen)
                val existing = setOf("layout_portrait", "layout_landscape", "integer_scaling", "filter", "voxel_resolution", "voxel_aa",
                    "keep_screen_on", "controls_visibility", "controls_opacity", "controls_size", "circle_pad", "haptics", "gamepad_mapping",
                    "dual_display", "dual_scaling", "top_display", "dual_controls", "qol", "qol_fast_forward", "qol_speed", "qol_shiny",
                    "qol_shared_exp", "qol_backups", "restore_backup", "qol_protect_shinies", "mystery_events", "mystery_events_intro",
                    "import_pak", "import_save", "export_save", "data_location", "about")
                val keys = descendants(root).mapNotNull { it.key }
                assertEquals(keys.size, keys.toSet().size)
                assertEquals(existing + setOf("display", "controls", "gameplay", "saves_data"), keys.toSet())
                val display = root.findPreference<PreferenceScreen>("display")!!
                assertEquals(listOf("dual_display", "top_display", "dual_scaling", "dual_controls"),
                    children(display.getPreference(0) as PreferenceGroup).map { it.key })
                assertEquals(listOf("qol", "mystery_events"), children(root.findPreference<PreferenceScreen>("gameplay")!!).map { it.key })
                val saves = root.findPreference<PreferenceScreen>("saves_data")!!
                assertNotNull(saves.findPreference<Preference>("qol_backups"))
                assertNotNull(saves.findPreference<Preference>("restore_backup"))
                assertNull(root.findPreference<PreferenceScreen>("qol")!!.findPreference<Preference>("qol_backups"))
                assertFalse(saves.findPreference<Preference>("data_location")!!.isSelectable)
                assertEquals(GameplayOptions(), GameplayOptions.load(context))
                val settings = AppSettings.load(context)
                assertEquals(2, settings.voxelScale)
                assertEquals(0, settings.voxelAASamples)
                assertFalse(settings.linearFilter)
                assertEquals(DualScaling.FILL, settings.dualScaling)
                assertTrue(settings.dualDisplay)
            }
        }
    }

    @Test fun nestedPagesAndRecreationPreserveStoredChoices() {
        preferences.edit().putString("layout_portrait", "console").putString("layout_landscape", "stacked")
            .putBoolean("integer_scaling", true).putString("filter", "linear").putString("voxel_resolution", "3")
            .putString("voxel_aa", "4").putBoolean("keep_screen_on", false).putString("controls_visibility", "always")
            .putInt("controls_size", 135).putInt("controls_opacity", 80).putBoolean("circle_pad", true)
            .putBoolean("haptics", false).putString("gamepad_mapping", "label").putString("dual_scaling", "fit")
            .putString("top_display", "second").putBoolean("dual_controls", true).putBoolean("qol_backups", true)
            .putBoolean("qol_fast_forward", true).putString("qol_speed", "8").putString("qol_shiny", "32")
            .putBoolean("qol_shared_exp", true).putBoolean("qol_protect_shinies", true).commit()
        val settings = AppSettings.load(context)
        val gameplay = GameplayOptions.load(context)
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            ready(scenario, null)
            for (page in listOf("display", "controls", "saves_data")) { open(scenario, page); back(scenario, null) }
            open(scenario, "gameplay"); open(scenario, "qol")
            scenario.recreate(); ready(scenario, "qol")
            assertEquals(settings, AppSettings.load(context))
            assertEquals(gameplay, GameplayOptions.load(context))
            back(scenario, "gameplay"); back(scenario, null)
            open(scenario, "saves_data")
            scenario.onActivity { assertTrue(fragment(it).findPreference<SwitchPreferenceCompat>("qol_backups")!!.isChecked) }
        }
    }

    @Test fun controllerFocusReturnsToParentRowAndSurvivesRecreation() {
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            ready(scenario, null); touchToolbar(scenario)
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "display")
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "controls")
            key(scenario, KeyEvent.KEYCODE_ENTER); ready(scenario, "controls"); focus(scenario, "gamepad_mapping")
            back(scenario, null); focus(scenario, "controls")
            scenario.recreate(); ready(scenario, null); focus(scenario, "controls")
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "gameplay")
            key(scenario, KeyEvent.KEYCODE_ENTER); ready(scenario, "gameplay"); focus(scenario, "qol")
            key(scenario, KeyEvent.KEYCODE_ENTER); ready(scenario, "qol"); focus(scenario, "qol_fast_forward")
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "qol_shiny")
            scenario.recreate(); ready(scenario, "qol"); focus(scenario, "qol_shiny")
            back(scenario, "gameplay"); focus(scenario, "qol")
            back(scenario, null); focus(scenario, "gameplay")
        }
    }

    @Test fun controllerSkipsDisabledRowsWhenTheyAreRecycledIntoView() {
        preferences.edit().putBoolean("dual_display", false).putBoolean("qol_fast_forward", false).commit()
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            ready(scenario, null); touchToolbar(scenario); open(scenario, "display")
            scenario.onActivity { activity ->
                val list = fragment(activity).listView
                list.layoutParams = list.layoutParams.apply { height = (150 * context.resources.displayMetrics.density).toInt() }
                list.requestLayout()
            }
            inst.waitForIdleSync()
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "dual_display")
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "filter")
            key(scenario, KeyEvent.KEYCODE_DPAD_UP); focus(scenario, "dual_display")
            scenario.onActivity { activity ->
                val current = fragment(activity)
                for (key in listOf("top_display", "dual_scaling", "dual_controls"))
                    assertFalse(current.findPreference<Preference>(key)!!.isEnabled)
            }
            back(scenario, null); open(scenario, "gameplay"); open(scenario, "qol")
            scenario.onActivity { activity ->
                val list = fragment(activity).listView
                list.layoutParams = list.layoutParams.apply { height = (150 * context.resources.displayMetrics.density).toInt() }
                list.requestLayout()
            }
            inst.waitForIdleSync()
            focus(scenario, "qol_fast_forward")
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "qol_shiny")
        }
    }

    @Test fun touchThenControllerFocusesAnActionWithoutActivatingOrSkippingIt() {
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            ready(scenario, null); touchToolbar(scenario)
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "display")
            scenario.onActivity { assertEquals(0, it.supportFragmentManager.backStackEntryCount) }
            touchToolbar(scenario)
            open(scenario, "gameplay"); open(scenario, "qol")
            touchToolbar(scenario)
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "qol_fast_forward")
            scenario.onActivity { assertFalse(fragment(it).findPreference<SwitchPreferenceCompat>("qol_fast_forward")!!.isChecked) }
            key(scenario, KeyEvent.KEYCODE_DPAD_DOWN); focus(scenario, "qol_shiny")
        }
    }

    @Test fun savesWorkSurvivesBackReopenAndActivityRecreation() {
        val started = CountDownLatch(1)
        val finish = CountDownLatch(1)
        lateinit var model: GameFilesModel
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            try {
                ready(scenario, null); open(scenario, "saves_data")
                scenario.onActivity { activity ->
                    model = (fragment(activity) as SettingsActivity.SettingsFragment).fileModel
                    model.submit(GameFilesModel.Action.EXPORT_SAVE) {
                        check(Looper.myLooper() != Looper.getMainLooper())
                        started.countDown()
                        check(finish.await(10, TimeUnit.SECONDS))
                        false
                    }
                }
                assertTrue(started.await(3, TimeUnit.SECONDS))
                back(scenario, null)
                scenario.onActivity { assertSame(model, (fragment(it) as SettingsActivity.SettingsFragment).fileModel) }
                open(scenario, "saves_data")
                scenario.recreate(); ready(scenario, "saves_data")
                scenario.onActivity {
                    val current = fragment(it) as SettingsActivity.SettingsFragment
                    assertSame(model, current.fileModel)
                    assertTrue(model.state.value!!.busy)
                    assertFalse(current.findPreference<Preference>("import_save")!!.isEnabled)
                }
                finish.countDown()
                waitFor("completed file work remained busy after navigation") {
                    var done = false
                    scenario.onActivity { done = model.state.value == null }
                    done
                }
                scenario.onActivity { assertTrue(fragment(it).findPreference<Preference>("import_save")!!.isEnabled) }
            } finally { finish.countDown() }
        }
    }

    @Test fun nestedImportPickerResultAndCancellationKeepExistingContract() {
        val payload = EmeraldSaveFixture.create(112, 114)
        val input = File(context.cacheDir, "hierarchy-import.sav").apply { writeBytes(payload) }
        val picker = IntentFilter(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); addCategory(Intent.CATEGORY_DEFAULT); addDataType("*/*")
        }
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            ready(scenario, null); open(scenario, "saves_data")
            lateinit var model: GameFilesModel
            scenario.onActivity { model = (fragment(it) as SettingsActivity.SettingsFragment).fileModel }
            val pending = File(model.files.dataDir, GameFiles.SAVE_NAME + ".import")
            val oldPending = pending.takeIf { it.isFile }?.readBytes()
            val oldSave = model.files.saveFile.takeIf { it.isFile }?.readBytes()
            try {
                pending.delete() // distinguish this result from an earlier identical staged fixture
                val result = Instrumentation.ActivityResult(Activity.RESULT_OK, Intent().setData(Uri.fromFile(input)))
                val accepted = inst.addMonitor(picker, result, true)
                try {
                    scenario.onActivity { fragment(it).findPreference<Preference>("import_save")!!.performClick() }
                    waitFor("nested picker result did not stage the selected save") { pending.isFile && pending.readBytes().contentEquals(payload) }
                    assertEquals(1, accepted.hits)
                } finally { inst.removeMonitor(accepted) }
                waitFor("import did not finish") {
                    var ready = false
                    scenario.onActivity { ready = model.state.value?.busy != true }
                    ready
                }
                // Accessibility can publish the dialog after its LiveData
                // completion. Wait for either auto-consumption or visible Later.
                waitFor("import completion was not acknowledged") {
                    var consumed = false
                    scenario.onActivity { consumed = model.state.value == null }
                    if (!consumed) inst.uiAutomation.rootInActiveWindow
                        ?.findAccessibilityNodeInfosByText(context.getString(R.string.import_later))
                        ?.firstOrNull { it.isClickable }?.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                    consumed
                }
                back(scenario, null); open(scenario, "saves_data")
                scenario.onActivity { assertSame(model, (fragment(it) as SettingsActivity.SettingsFragment).fileModel) }
                val canceled = inst.addMonitor(picker, Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null), true)
                try {
                    scenario.onActivity { fragment(it).findPreference<Preference>("import_save")!!.performClick() }
                    inst.waitForIdleSync()
                    assertEquals(1, canceled.hits)
                    scenario.onActivity { assertNull(model.state.value) }
                    assertArrayEquals(payload, pending.readBytes())
                    if (oldSave == null) assertFalse(model.files.saveFile.exists())
                    else assertArrayEquals(oldSave, model.files.saveFile.readBytes())
                } finally { inst.removeMonitor(canceled) }
            } finally {
                if (oldPending == null) pending.delete() else pending.writeBytes(oldPending)
                input.delete()
            }
        }
    }
}
