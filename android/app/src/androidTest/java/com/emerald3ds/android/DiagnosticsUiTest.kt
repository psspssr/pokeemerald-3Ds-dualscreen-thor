package com.emerald3ds.android

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.content.IntentFilter
import androidx.appcompat.app.AlertDialog
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.checkbox.MaterialCheckBox
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class DiagnosticsUiTest {
    private val inst = InstrumentationRegistry.getInstrumentation()

    private fun about(activity: SettingsActivity): Pair<SettingsActivity.SettingsFragment, AlertDialog> {
        val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
        fragment.javaClass.getDeclaredMethod("showAbout").apply { isAccessible = true }.invoke(fragment)
        val dialog = fragment.javaClass.getDeclaredField("aboutDialog").apply { isAccessible = true }.get(fragment) as AlertDialog
        return fragment to dialog
    }

    @Test fun aboutRecordingIsOptInAndExportUsesCancelableJsonDocumentPicker() {
        inst.runOnMainSync { Diagnostics.setRecording(inst.targetContext, false) }
        val filter = IntentFilter(Intent.ACTION_CREATE_DOCUMENT).apply { addDataType("application/json") }
        val monitor = inst.addMonitor(filter, Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null), true)
        try {
            ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
                scenario.onActivity {
                    val (_, dialog) = about(it)
                    val check = dialog.findViewById<MaterialCheckBox>(R.id.diagnostics_recording)!!
                    assertFalse(check.isChecked)
                    check.performClick()
                    assertTrue(Diagnostics.isRecording())
                }
                scenario.recreate()
                scenario.onActivity {
                    val (fragment, dialog) = about(it)
                    assertTrue(dialog.findViewById<MaterialCheckBox>(R.id.diagnostics_recording)!!.isChecked)
                    fragment.diagnosticsModel.openOutput = { throw AssertionError("canceled picker opened an output") }
                    dialog.getButton(AlertDialog.BUTTON_NEUTRAL).performClick()
                }
                inst.waitForIdleSync()
                assertEquals("Export did not request a JSON document", 1, monitor.hits)
                scenario.onActivity {
                    val fragment = it.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
                    assertEquals(DiagnosticsExportModel.Phase.IDLE, fragment.diagnosticsModel.state.value!!.phase)
                }
            }
        } finally {
            inst.removeMonitor(monitor)
            inst.runOnMainSync { Diagnostics.setRecording(inst.targetContext, false) }
        }
    }
}
