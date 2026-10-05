package com.emerald3ds.android

import android.content.Context
import androidx.preference.ListPreference
import androidx.preference.Preference

/** A saved quality request is independent of the current GPU's resource limit. */
internal object VoxelResolution {
    fun scale(value: String?): Int = value?.toIntOrNull()?.takeIf { it in 1..4 } ?: 1

    internal fun effective(requested: Int, maximum: Int): Int =
        requested.coerceIn(1, 4).coerceAtMost(maximum.coerceIn(1, 4))

    private fun label(context: Context, scale: Int): String =
        context.resources.getStringArray(R.array.voxel_resolution_entries)[scale.coerceIn(1, 4) - 1]

    fun configure(preference: ListPreference) {
        val maximum = NativeBridge.voxelScaleCapabilities()
        val choices = (1..maximum.coerceIn(1, 4)).toList()
        preference.entries = choices.map { label(preference.context, it) }.toTypedArray()
        preference.entryValues = choices.map(Int::toString).toTypedArray()
        preference.summaryProvider = Preference.SummaryProvider<ListPreference> { current ->
            val requested = scale(current.value)
            val requestedLabel = label(current.context, requested)
            val quality = when {
                maximum < 0 -> current.context.getString(R.string.pref_voxel_resolution_unknown, requestedLabel)
                requested <= maximum -> requestedLabel
                else -> current.context.getString(R.string.pref_voxel_resolution_unavailable,
                    requestedLabel, label(current.context, effective(requested, maximum)))
            }
            "$quality\n${current.context.getString(R.string.pref_voxel_resolution_description)}"
        }
    }
}
