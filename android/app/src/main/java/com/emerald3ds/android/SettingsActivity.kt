package com.emerald3ds.android

import android.os.Bundle
import android.text.Html
import android.view.View
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.Toast
import android.widget.TextView
import android.content.ActivityNotFoundException
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AlertDialog
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceScreen
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.checkbox.MaterialCheckBox
import java.text.DateFormat
import java.util.Date

class SettingsActivity : AppCompatActivity(), PreferenceFragmentCompat.OnPreferenceStartScreenCallback {
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        GameActivity.observePausedKeyEvent(event)
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        GameActivity.observePausedMotionEvent(event)
        return super.dispatchGenericMotionEvent(event)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Diagnostics.configure(applicationContext)
        setContentView(R.layout.activity_settings)
        val root = findViewById<android.view.View>(R.id.settings_root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }
        findViewById<MaterialToolbar>(R.id.toolbar).apply {
            setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material)
            setNavigationOnClickListener { onBackPressedDispatcher.onBackPressed() }
        }
        if (savedInstanceState == null) {
            supportFragmentManager.beginTransaction().replace(R.id.settings_container, SettingsFragment()).commit()
        }
    }

    override fun onPreferenceStartScreen(caller: PreferenceFragmentCompat, pref: PreferenceScreen): Boolean {
        val fragment = (if (pref.key == "mystery_events") MysteryEventsFragment() else SettingsFragment()).apply {
            arguments = Bundle().apply { putString(PreferenceFragmentCompat.ARG_PREFERENCE_ROOT, pref.key) }
        }
        supportFragmentManager.beginTransaction().replace(R.id.settings_container, fragment)
            .addToBackStack(pref.key).commit()
        return true
    }

    class SettingsFragment : PreferenceFragmentCompat() {
        private lateinit var files: GameFiles
        internal lateinit var fileModel: GameFilesModel
            private set
        private var dialog: AlertDialog? = null
        internal lateinit var diagnosticsModel: DiagnosticsExportModel
            private set
        private var aboutDialog: AlertDialog? = null

        // Preserve alpha.7's registration order for in-flight picker results.
        // Append new contracts after the existing import/export launchers.
        private val importPak = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) import(uri, GameFiles.Kind.PAK)
        }
        private val importSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) import(uri, GameFiles.Kind.SAVE)
        }
        private val exportSave = registerForActivityResult(
            ActivityResultContracts.CreateDocument("application/octet-stream")
        ) { uri ->
            if (uri == null) return@registerForActivityResult
            fileModel.exportSave(uri)
        }

        private val exportDiagnostics = registerForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
            diagnosticsModel.destination(uri)
        }

        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.preferences, rootKey)
            fileModel = ViewModelProvider(this)[GameFilesModel::class.java]
            diagnosticsModel = ViewModelProvider(requireActivity())[DiagnosticsExportModel::class.java]
            files = fileModel.files
            click("import_pak") { importPak.launch(arrayOf("*/*")) }
            click("import_save") { importSave.launch(arrayOf("*/*")) }
            click("export_save") {
                if (files.saveFile.isFile) exportSave.launch(GameFiles.SAVE_NAME)
                else toast(getString(R.string.export_none))
            }
            click("about") { showAbout() }
            click("restore_backup") { showBackups() }
            findPreference<Preference>("data_location")?.summary = files.dataDir.absolutePath
            findPreference<Preference>("about")?.summary = getString(R.string.pref_version, BuildConfig.VERSION_NAME)
        }

        override fun onResume() {
            super.onResume()
            requireActivity().findViewById<MaterialToolbar>(R.id.toolbar).title =
                preferenceScreen.title ?: getString(R.string.settings_title)
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            diagnosticsModel.state.observe(viewLifecycleOwner) { state ->
                findPreference<Preference>("about")?.summary = if (state.phase == DiagnosticsExportModel.Phase.WRITING)
                    getString(R.string.diagnostics_exporting) else getString(R.string.pref_version, BuildConfig.VERSION_NAME)
                aboutDialog?.getButton(AlertDialog.BUTTON_NEUTRAL)?.isEnabled = state.phase == DiagnosticsExportModel.Phase.IDLE
                if (state.phase == DiagnosticsExportModel.Phase.COMPLETE) {
                    diagnosticsModel.consumeResult()
                    toast(getString(when (state.result) {
                        DiagnosticsExportModel.Result.EXPORTED -> R.string.diagnostics_exported
                        DiagnosticsExportModel.Result.PICKER_FAILED -> R.string.diagnostics_picker_failed
                        else -> R.string.diagnostics_failed
                    }))
                }
            }
            fileModel.state.observe(viewLifecycleOwner) { state ->
                val busy = state?.busy == true
                for (key in listOf("import_pak", "import_save", "export_save", "restore_backup"))
                    findPreference<Preference>(key)?.isEnabled = !busy
                findPreference<Preference>("data_location")?.summary =
                    if (busy) getString(R.string.file_working) else files.dataDir.absolutePath
                if (state == null || busy) return@observe
                val needsRestart = state.error == null &&
                    state.action != GameFilesModel.Action.EXPORT_SAVE && NativeBridge.isStarted()
                // Keep an unacknowledged restart choice through recreation.
                // Dismissing the old view's dialog must not lose this result.
                if (!needsRestart) fileModel.consumeResult()
                if (state.error != null) {
                    toast(getString(if (state.action == GameFilesModel.Action.EXPORT_SAVE)
                        R.string.export_failed else R.string.import_failed, state.error))
                } else if (state.action == GameFilesModel.Action.EXPORT_SAVE) {
                    toast(getString(R.string.export_done))
                } else if (!needsRestart) {
                    toast(getString(R.string.import_done))
                } else {
                    dialog = MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.import_restart_title)
                        .setMessage(R.string.import_restart_body)
                        .setPositiveButton(R.string.restart_now) { _, _ ->
                            fileModel.consumeResult()
                            RestartActivity.restart(requireActivity())
                        }
                        .setNegativeButton(R.string.import_later) { _, _ -> fileModel.consumeResult() }
                        .setOnCancelListener { fileModel.consumeResult() }
                        .show().also(::observeGameReleases)
                }
            }
        }

        override fun onDestroyView() {
            aboutDialog?.dismiss()
            aboutDialog = null
            dialog?.dismiss()
            dialog = null
            super.onDestroyView()
        }

        private fun click(key: String, action: () -> Unit) {
            findPreference<Preference>(key)?.setOnPreferenceClickListener {
                action()
                true
            }
        }

        private fun toast(message: String) = Toast.makeText(requireContext(), message, Toast.LENGTH_LONG).show()

        private fun showBackups() {
            val backups = files.saveBackups()
            if (backups.isEmpty()) { toast(getString(R.string.restore_backup_none)); return }
            val format = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.MEDIUM)
            val labels = backups.map { format.format(Date(files.backupTime(it) ?: it.lastModified())) }.toTypedArray()
            dialog = MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.restore_backup)
                .setItems(labels) { _, which ->
                    dialog = MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.restore_backup)
                        .setMessage(getString(R.string.restore_backup_confirm, labels[which]))
                        .setNegativeButton(android.R.string.cancel, null)
                        .setPositiveButton(R.string.restore_backup_action) { _, _ -> fileModel.restoreBackup(backups[which]) }
                        .show().also(::observeGameReleases)
                }.setNegativeButton(android.R.string.cancel, null).show().also(::observeGameReleases)
        }

        /*
         * The running game holds the pack open and the save in memory, so an
         * import is staged next to the file and moved into place by the next
         * process before the game starts.
         */
        private fun import(uri: android.net.Uri, kind: GameFiles.Kind) {
            fileModel.importFile(uri, kind)
        }

        private fun observeGameReleases(dialog: AlertDialog) {
            dialog.setOnKeyListener { _, _, event -> GameActivity.observePausedKeyEvent(event); false }
            dialog.window?.decorView?.setOnGenericMotionListener { _, event ->
                GameActivity.observePausedMotionEvent(event)
                false
            }
        }

        private fun showAbout() {
            @Suppress("DEPRECATION")
            val body = Html.fromHtml(getString(R.string.about_body, BuildConfig.VERSION_NAME))
            val content = layoutInflater.inflate(R.layout.dialog_about, null)
            content.findViewById<TextView>(R.id.about_body).text = body
            content.findViewById<MaterialCheckBox>(R.id.diagnostics_recording).apply {
                isChecked = Diagnostics.isRecording()
                setOnCheckedChangeListener { _, enabled -> Diagnostics.setRecording(requireContext(), enabled) }
            }
            aboutDialog = MaterialAlertDialogBuilder(requireContext()).setTitle(R.string.about_title).setView(content)
                .setPositiveButton(R.string.ok, null)
                .setNeutralButton(R.string.diagnostics_export) { _, _ ->
                    diagnosticsModel.begin()?.let { filename ->
                        try { exportDiagnostics.launch(filename) }
                        catch (_: ActivityNotFoundException) { diagnosticsModel.pickerFailed() }
                    }
                }.create().also { about ->
                    about.setOnDismissListener { if (aboutDialog === about) aboutDialog = null }
                    about.show()
                    observeGameReleases(about)
                    about.getButton(AlertDialog.BUTTON_NEUTRAL).isEnabled =
                        diagnosticsModel.state.value?.phase == DiagnosticsExportModel.Phase.IDLE
                }
        }
    }
}
