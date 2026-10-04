package com.emerald3ds.android

import android.content.Context
import androidx.preference.ListPreference
import androidx.preference.Preference

/** Renderer-supported choices; preferences retain the requested quality. */
internal object VoxelAntiAliasing {
    fun samples(value: String?): Int = value?.toIntOrNull()?.takeIf { it == 2 || it == 4 } ?: 0

    private fun supported(samples: Int, capabilities: Int): Boolean = capabilities >= 0 && when (samples) {
        2 -> capabilities and 1 != 0
        4 -> capabilities and 2 != 0
        else -> true
    }

    internal fun effective(requested: Int, capabilities: Int): Int = when {
        requested == 4 && supported(4, capabilities) -> 4
        requested >= 2 && supported(2, capabilities) -> 2
        else -> 0
    }

    private fun label(context: Context, samples: Int): String =
        context.resources.getStringArray(R.array.voxel_aa_entries)[when (samples) { 2 -> 1; 4 -> 2; else -> 0 }]

    fun configure(preference: ListPreference) {
        val capabilities = NativeBridge.voxelAACapabilities()
        val choices = listOf(0, 2, 4).filter { it == 0 || supported(it, capabilities) }
        preference.entries = choices.map { label(preference.context, it) }.toTypedArray()
        preference.entryValues = choices.map(Int::toString).toTypedArray()
        preference.summaryProvider = Preference.SummaryProvider<ListPreference> { current ->
            val requested = samples(current.value)
            val requestedLabel = label(current.context, requested)
            when {
                capabilities < 0 -> current.context.getString(R.string.pref_voxel_aa_unknown, requestedLabel)
                requested == 0 || supported(requested, capabilities) -> requestedLabel
                else -> current.context.getString(R.string.pref_voxel_aa_unavailable, requestedLabel,
                    label(current.context, effective(requested, capabilities)))
            }
        }
    }
}
