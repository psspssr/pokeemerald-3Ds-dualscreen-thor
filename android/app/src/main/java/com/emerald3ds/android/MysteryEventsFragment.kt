package com.emerald3ds.android

import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.lifecycle.ViewModelProvider
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder

/** Per-adventure actions, deliberately separate from persistent app options. */
class MysteryEventsFragment : PreferenceFragmentCompat() {
    internal var modelFactory: ViewModelProvider.Factory = MysteryEventsModel.Factory()
    internal lateinit var model: MysteryEventsModel
        private set
    private var dialog: AlertDialog? = null
    private var selectedEvent: MysteryEvent? = null

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.preferences, "mystery_events")
        model = ViewModelProvider(this, modelFactory)[MysteryEventsModel::class.java]
        for (event in MysteryEvent.catalogue) {
            preferenceScreen.addPreference(Preference(requireContext()).apply {
                key = event.key
                setTitle(event.title)
                isSingleLineTitle = false
                isIconSpaceReserved = false
                isPersistent = false
                setOnPreferenceClickListener { showEvent(event); true }
            })
        }
        preferenceScreen.addPreference(Preference(requireContext()).apply {
            key = "mystery_events_refresh"
            setTitle(R.string.mystery_events_refresh)
            isIconSpaceReserved = false
            isPersistent = false
            setOnPreferenceClickListener { model.refresh(); true }
        })
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        model.state.observe(viewLifecycleOwner) { state ->
            for (event in MysteryEvent.catalogue) {
                val status = when {
                    state.busy && state.event == event.id -> getString(R.string.mystery_events_working)
                    state.busy && state.statuses.isEmpty() -> getString(R.string.mystery_events_loading)
                    else -> statusDescription(event, state)
                }
                findPreference<Preference>(event.key)?.summary =
                    getString(R.string.mystery_events_status_description, status, getString(event.summary))
            }
            findPreference<Preference>("mystery_events_refresh")?.isEnabled = !state.busy
            updateDialog(state)
            if (state.result != null && !state.busy) {
                model.consumeResult()
                Toast.makeText(requireContext(), resultMessage(state), Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        requireActivity().findViewById<MaterialToolbar>(R.id.toolbar).setTitle(R.string.mystery_events_title)
        model.refresh()
    }

    override fun onDestroyView() {
        dialog?.dismiss()
        dialog = null
        selectedEvent = null
        super.onDestroyView()
    }

    private fun showEvent(event: MysteryEvent) {
        dialog?.dismiss()
        val shown = MaterialAlertDialogBuilder(requireContext())
            .setTitle(event.title)
            .setMessage(R.string.mystery_events_loading)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.mystery_events_activate) { _, _ -> model.activate(event.id) }
            .create()
        dialog = shown
        selectedEvent = event
        shown.setOnDismissListener {
            if (dialog === shown) {
                dialog = null
                selectedEvent = null
            }
        }
        shown.show()
        updateDialog(model.state.value ?: MysteryEventsModel.State())
    }

    private fun updateDialog(state: MysteryEventsModel.State) {
        val event = selectedEvent ?: return
        val shown = dialog ?: return
        val status = when {
            state.busy -> getString(R.string.mystery_events_loading)
            else -> statusDescription(event, state)
        }
        shown.setMessage(getString(R.string.mystery_events_details, getString(event.description), status))
        shown.getButton(AlertDialog.BUTTON_POSITIVE).apply {
            isEnabled = state.canActivate(event.id)
            visibility = if (isEnabled) View.VISIBLE else View.GONE
        }
    }

    private fun statusDescription(event: MysteryEvent, state: MysteryEventsModel.State): String =
        if (state.status(event.id) == MysteryEventStatus.PREREQUISITE)
            getString(R.string.mystery_events_prerequisite_detail, getString(event.prerequisite))
        else getString(state.status(event.id).label)

    private fun resultMessage(state: MysteryEventsModel.State): Int = when (state.result) {
        MysteryEventsModel.RESULT_ACTIVATED -> R.string.mystery_events_activated
        MysteryEventsModel.RESULT_ALREADY -> R.string.mystery_events_already
        MysteryEventsModel.RESULT_NO_GAME -> R.string.mystery_events_no_game
        MysteryEventsModel.RESULT_BUSY -> R.string.mystery_events_busy
        MysteryEventsModel.RESULT_NO_SPACE -> when (state.status(state.event ?: -1)) {
            MysteryEventStatus.BAG_FULL -> R.string.mystery_events_bag_full
            MysteryEventStatus.PARTY_FULL -> R.string.mystery_events_party_full
            MysteryEventStatus.DECOR_FULL -> R.string.mystery_events_decor_full
            else -> R.string.mystery_events_no_space
        }
        MysteryEventsModel.RESULT_TIMEOUT -> R.string.mystery_events_timeout
        else -> R.string.mystery_events_failed
    }
}
