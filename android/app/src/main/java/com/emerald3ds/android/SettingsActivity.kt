package com.emerald3ds.android

import android.os.Bundle
import android.text.Html
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AlertDialog
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class SettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)
        val root = findViewById<android.view.View>(R.id.settings_root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }
        findViewById<MaterialToolbar>(R.id.toolbar).apply {
            setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material)
            setNavigationOnClickListener { finish() }
        }
        if (savedInstanceState == null) {
            supportFragmentManager.beginTransaction().replace(R.id.settings_container, SettingsFragment()).commit()
        }
    }

    class SettingsFragment : PreferenceFragmentCompat() {
        private lateinit var files: GameFiles
        internal lateinit var fileModel: GameFilesModel
            private set
        private var dialog: AlertDialog? = null

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

        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.preferences, rootKey)
            fileModel = ViewModelProvider(this)[GameFilesModel::class.java]
            files = fileModel.files
            click("import_pak") { importPak.launch(arrayOf("*/*")) }
            click("import_save") { importSave.launch(arrayOf("*/*")) }
            click("export_save") {
                if (files.saveFile.isFile) exportSave.launch(GameFiles.SAVE_NAME)
                else toast(getString(R.string.export_none))
            }
            click("about") { showAbout() }
            findPreference<Preference>("data_location")?.summary = files.dataDir.absolutePath
            findPreference<Preference>("about")?.summary = getString(R.string.pref_version, BuildConfig.VERSION_NAME)
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            fileModel.state.observe(viewLifecycleOwner) { state ->
                val busy = state?.busy == true
                for (key in listOf("import_pak", "import_save", "export_save"))
                    findPreference<Preference>(key)?.isEnabled = !busy
                findPreference<Preference>("data_location")?.summary =
                    if (busy) getString(R.string.file_working) else files.dataDir.absolutePath
                if (state == null || busy) return@observe
                fileModel.consumeResult()
                if (state.error != null) {
                    toast(getString(if (state.action == GameFilesModel.Action.EXPORT_SAVE)
                        R.string.export_failed else R.string.import_failed, state.error))
                } else if (state.action == GameFilesModel.Action.EXPORT_SAVE) {
                    toast(getString(R.string.export_done))
                } else if (!NativeBridge.isStarted()) {
                    toast(getString(R.string.import_done))
                } else {
                    dialog = MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.import_restart_title)
                        .setMessage(R.string.import_restart_body)
                        .setPositiveButton(R.string.restart_now) { _, _ -> RestartActivity.restart(requireActivity()) }
                        .setNegativeButton(R.string.import_later, null)
                        .show()
                }
            }
        }

        override fun onDestroyView() {
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

        /*
         * The running game holds the pack open and the save in memory, so an
         * import is staged next to the file and moved into place by the next
         * process before the game starts.
         */
        private fun import(uri: android.net.Uri, kind: GameFiles.Kind) {
            fileModel.importFile(uri, kind)
        }

        private fun showAbout() {
            @Suppress("DEPRECATION")
            val body = Html.fromHtml(getString(R.string.about_body, BuildConfig.VERSION_NAME))
            dialog = MaterialAlertDialogBuilder(requireContext()).setTitle(R.string.about_title).setMessage(body)
                .setPositiveButton(R.string.ok, null).show()
        }
    }
}
