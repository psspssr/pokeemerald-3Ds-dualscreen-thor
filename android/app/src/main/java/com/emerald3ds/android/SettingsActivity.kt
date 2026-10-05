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
import androidx.core.view.doOnNextLayout
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.preference.Preference
import androidx.preference.PreferenceDialogFragmentCompat
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceScreen
import androidx.preference.PreferenceGroup
import androidx.recyclerview.widget.RecyclerView
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.checkbox.MaterialCheckBox
import java.text.DateFormat
import java.util.Date

class SettingsActivity : AppCompatActivity(), PreferenceFragmentCompat.OnPreferenceStartScreenCallback {
    private val preferenceFocus = mutableMapOf<String, String>()
    private val restoreFocus = mutableSetOf<String>()
    private var pendingFocusLayout: RecyclerView? = null
    private val preferenceLists = mutableMapOf<Fragment, Pair<RecyclerView, RecyclerView.OnChildAttachStateChangeListener>>()
    private val preferenceDialogs = object : FragmentManager.FragmentLifecycleCallbacks() {
        override fun onFragmentViewCreated(manager: FragmentManager, fragment: Fragment, view: View,
                                           savedInstanceState: Bundle?) {
            if (fragment is PreferenceFragmentCompat) {
                restoreFocus += pageKey(fragment)
                val list = fragment.listView
                val listener = object : RecyclerView.OnChildAttachStateChangeListener {
                    override fun onChildViewAttachedToWindow(view: View) {
                        val preference = preferenceRows(fragment)[list.getChildAdapterPosition(view)]
                        view.isFocusable = preference?.let { it.isEnabled && it.isSelectable } == true
                    }
                    override fun onChildViewDetachedFromWindow(view: View) = Unit
                }
                list.addOnChildAttachStateChangeListener(listener)
                preferenceLists[fragment] = list to listener
                view.post { MenuNavigation.refreshFocus(window) }
            }
        }

        override fun onFragmentResumed(manager: FragmentManager, fragment: Fragment) {
            if (fragment is PreferenceFragmentCompat)
                fragment.view?.post { MenuNavigation.refreshFocus(window) }
        }

        override fun onFragmentPaused(manager: FragmentManager, fragment: Fragment) {
            if (fragment is PreferenceFragmentCompat) rememberFocus(fragment)
        }

        override fun onFragmentViewDestroyed(manager: FragmentManager, fragment: Fragment) {
            preferenceLists.remove(fragment)?.let { (list, listener) ->
                list.removeOnChildAttachStateChangeListener(listener)
            }
            pendingFocusLayout = null
        }

        override fun onFragmentStarted(manager: FragmentManager, fragment: Fragment) {
            // DialogFragment has shown its window by onStart. This also
            // covers a preference dialog restored after Activity recreation.
            if (fragment is PreferenceDialogFragmentCompat)
                fragment.dialog?.let(::observePausedGameInput)
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        GameActivity.observePausedKeyEvent(event)
        val handled = super.dispatchKeyEvent(event) || movePreferenceFocus(event)
        currentPreferences()?.let(::rememberFocus)
        return handled
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        GameActivity.observePausedMotionEvent(event)
        return super.dispatchGenericMotionEvent(event)
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN)
            currentPreferences()?.let { restoreFocus += pageKey(it) }
        return super.dispatchTouchEvent(event)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Diagnostics.configure(applicationContext)
        supportFragmentManager.registerFragmentLifecycleCallbacks(preferenceDialogs, true)
        setContentView(R.layout.activity_settings)
        savedInstanceState?.getBundle("preference_focus")?.let { saved ->
            for (key in saved.keySet()) saved.getString(key)?.let { preferenceFocus[key] = it }
        }
        MenuNavigation.install(window, ::ensurePreferenceFocus)
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

    override fun onDestroy() {
        for ((list, listener) in preferenceLists.values)
            list.removeOnChildAttachStateChangeListener(listener)
        preferenceLists.clear()
        supportFragmentManager.unregisterFragmentLifecycleCallbacks(preferenceDialogs)
        super.onDestroy()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        currentPreferences()?.let(::rememberFocus)
        outState.putBundle("preference_focus", Bundle().apply {
            for ((page, key) in preferenceFocus) putString(page, key)
        })
        super.onSaveInstanceState(outState)
    }

    private fun currentPreferences() = supportFragmentManager.findFragmentById(R.id.settings_container)
        as? PreferenceFragmentCompat

    private fun pageKey(fragment: PreferenceFragmentCompat) =
        fragment.arguments?.getString(PreferenceFragmentCompat.ARG_PREFERENCE_ROOT)
            ?: fragment.preferenceScreen?.key ?: "root"

    private fun preferenceRows(fragment: PreferenceFragmentCompat): Map<Int, Preference> {
        val positions = fragment.listView.adapter as? PreferenceGroup.PreferencePositionCallback ?: return emptyMap()
        val rows = mutableMapOf<Int, Preference>()
        fun visit(group: PreferenceGroup) {
            for (i in 0 until group.preferenceCount) {
                val preference = group.getPreference(i)
                val position = positions.getPreferenceAdapterPosition(preference)
                if (position >= 0) rows[position] = preference
                if (preference is PreferenceGroup) visit(preference)
            }
        }
        visit(fragment.preferenceScreen)
        return rows
    }

    private fun focusedPreference(fragment: PreferenceFragmentCompat): Preference? {
        if (fragment.view == null) return null
        val list = fragment.listView
        val focused = list.findFocus() ?: return null
        val row = list.findContainingItemView(focused) ?: return null
        val position = list.getChildAdapterPosition(row)
        return preferenceRows(fragment)[position]
    }

    private fun rememberFocus(fragment: PreferenceFragmentCompat) {
        if (!MenuNavigation.usingController || pageKey(fragment) in restoreFocus) return
        focusedPreference(fragment)?.takeIf { it.isEnabled && it.isSelectable }?.key?.let {
            preferenceFocus[pageKey(fragment)] = it
        }
    }

    private fun movePreferenceFocus(event: KeyEvent): Boolean {
        if (!MenuNavigation.usingController || event.action != KeyEvent.ACTION_DOWN ||
            event.keyCode !in listOf(KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN)) return false
        val fragment = currentPreferences()?.takeIf { it.view != null } ?: return false
        val list = fragment.listView
        val focused = list.findFocus() ?: return false
        val row = list.findContainingItemView(focused) ?: return false
        val current = list.getChildAdapterPosition(row)
        if (current == RecyclerView.NO_POSITION) return false
        val eligible = preferenceRows(fragment).filterValues { it.isEnabled && it.isSelectable }.toSortedMap()
        val target = if (event.keyCode == KeyEvent.KEYCODE_DPAD_DOWN)
            eligible.entries.firstOrNull { it.key > current }
        else eligible.entries.lastOrNull { it.key < current }
        val key = target?.value?.key ?: return false // let Android reach the toolbar at either boundary
        // RecyclerView's default focus search only lays out a bounded area.
        // A run of offscreen disabled rows can be longer than that area.
        // Use the real preference positions, then restore this exact row.
        val page = pageKey(fragment)
        preferenceFocus[page] = key
        restoreFocus += page
        ensurePreferenceFocus()
        return true
    }

    /** Establish or restore a valid row after native controls handle input. */
    private fun ensurePreferenceFocus(): Boolean {
        if (!MenuNavigation.usingController || !window.decorView.hasWindowFocus()) return false
        val fragment = currentPreferences()?.takeIf { it.view != null } ?: return false
        val list = fragment.listView
        val rows = preferenceRows(fragment)
        fun eligible(position: Int): Boolean = rows[position]?.let { it.isEnabled && it.isSelectable } == true
        // AndroidX binds focusable from selectable alone. Disabled dependency
        // rows must also be skipped when a D-pad moves through this list.
        for (i in 0 until list.childCount) {
            val child = list.getChildAt(i)
            child.isFocusable = eligible(list.getChildAdapterPosition(child))
        }
        val page = pageKey(fragment)
        if (page !in restoreFocus && window.currentFocus != null) {
            val focused = focusedPreference(fragment)
            if (focused?.let { it.isEnabled && it.isSelectable } == true || !list.hasFocus()) return false
        }
        val remembered = rows.entries.firstOrNull { it.value.key == preferenceFocus[page] }?.key
        val position = remembered?.takeIf(::eligible)
            ?: rows.keys.sorted().firstOrNull(::eligible) ?: return false
        val row = list.findViewHolderForAdapterPosition(position)?.itemView
        if (row != null) {
            val restored = page in restoreFocus
            restoreFocus -= page
            rows[position]?.key?.let { preferenceFocus[page] = it }
            return if (row.hasFocus() && !row.isInTouchMode) restored else row.requestFocusFromTouch()
        }
        // A saved row can be outside the recreated viewport. Wait for that
        // layout once; do not spin a posted focus loop before a frame arrives.
        if (pendingFocusLayout !== list) {
            pendingFocusLayout = list
            list.doOnNextLayout {
                if (pendingFocusLayout === list) pendingFocusLayout = null
                MenuNavigation.refreshFocus(window)
            }
            list.scrollToPosition(position)
            list.requestLayout()
        }
        return list.requestFocusFromTouch()
    }

    override fun onPreferenceStartScreen(caller: PreferenceFragmentCompat, pref: PreferenceScreen): Boolean {
        pref.key?.let { preferenceFocus[pageKey(caller)] = it }
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
            AppSettings.prepareDefaults(requireContext())
            setPreferencesFromResource(R.xml.preferences, rootKey)
            // Nested pages share in-flight work and unacknowledged results.
            // Only the currently attached view observes/presents that result.
            fileModel = ViewModelProvider(requireActivity())[GameFilesModel::class.java]
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
            findPreference<ListPreference>("voxel_aa")?.let(VoxelAntiAliasing::configure)
            findPreference<ListPreference>("voxel_resolution")?.let(VoxelResolution::configure)
        }

        override fun onResume() {
            super.onResume()
            requireActivity().findViewById<MaterialToolbar>(R.id.toolbar).title =
                preferenceScreen.title ?: getString(R.string.settings_title)
            findPreference<ListPreference>("voxel_aa")?.let(VoxelAntiAliasing::configure)
            findPreference<ListPreference>("voxel_resolution")?.let(VoxelResolution::configure)
        }

        override fun onDisplayPreferenceDialog(preference: Preference) {
            if (preference.key == "voxel_aa" && preference is ListPreference)
                VoxelAntiAliasing.configure(preference)
            if (preference.key == "voxel_resolution" && preference is ListPreference)
                VoxelResolution.configure(preference)
            super.onDisplayPreferenceDialog(preference)
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
                        .show().also(::observePausedGameInput)
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
                        .show().also(::observePausedGameInput)
                }.setNegativeButton(android.R.string.cancel, null).show().also(::observePausedGameInput)
        }

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
                    observePausedGameInput(about)
                    about.getButton(AlertDialog.BUTTON_NEUTRAL).isEnabled =
                        diagnosticsModel.state.value?.phase == DiagnosticsExportModel.Phase.IDLE
                }
        }
    }
}
