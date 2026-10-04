package com.emerald3ds.android

import android.content.Context
import androidx.preference.PreferenceManager

enum class ControlsVisibility { AUTO, ALWAYS, NEVER }
enum class DualScaling { FILL, FIT }

data class AppSettings(
    val portraitLayout: String,
    val landscapeLayout: String,
    val integerScaling: Boolean,
    val linearFilter: Boolean,
    val keepScreenOn: Boolean,
    val controlsVisibility: ControlsVisibility,
    val controlsOpacity: Float,
    val controlsScale: Float,
    val circlePad: Boolean,
    val haptics: Boolean,
    val labelMapping: Boolean,
    val dualDisplay: Boolean,
    val dualScaling: DualScaling,
    val topOnSecondDisplay: Boolean,
    val dualControls: Boolean,
    val voxelAASamples: Int,
) {
    companion object {
        const val PORTRAIT_FILL = "fill"
        const val PORTRAIT_CONSOLE = "console"
        const val LANDSCAPE_SIDE_BY_SIDE = "side_by_side"
        const val LANDSCAPE_TOP_LARGE = "top_large"
        const val LANDSCAPE_TOP_ONLY = "top_only"
        const val LANDSCAPE_STACKED = "stacked"

        internal fun prepareDefaults(context: Context) {
            val p = PreferenceManager.getDefaultSharedPreferences(context)
            // Older previews persisted Linear as their default. Switch those
            // installs to sharp pixels once; a later Smooth choice stays put.
            if (!p.getBoolean("sharp_filter_defaults_applied", false)) {
                val edit = p.edit().putBoolean("sharp_filter_defaults_applied", true)
                if (p.getString("filter", null) == "linear") edit.putString("filter", "nearest")
                edit.apply()
            }
            PreferenceManager.setDefaultValues(context, R.xml.preferences, false)
        }

        fun load(context: Context): AppSettings {
            prepareDefaults(context)
            val p = PreferenceManager.getDefaultSharedPreferences(context)
            return AppSettings(
                portraitLayout = p.getString("layout_portrait", PORTRAIT_FILL) ?: PORTRAIT_FILL,
                landscapeLayout = p.getString("layout_landscape", LANDSCAPE_SIDE_BY_SIDE) ?: LANDSCAPE_SIDE_BY_SIDE,
                integerScaling = p.getBoolean("integer_scaling", false),
                linearFilter = p.getString("filter", "nearest") == "linear",
                keepScreenOn = p.getBoolean("keep_screen_on", true),
                controlsVisibility = when (p.getString("controls_visibility", "auto")) {
                    "always" -> ControlsVisibility.ALWAYS
                    "never" -> ControlsVisibility.NEVER
                    else -> ControlsVisibility.AUTO
                },
                controlsOpacity = p.getInt("controls_opacity", 55).coerceIn(10, 100) / 100f,
                controlsScale = p.getInt("controls_size", 100).coerceIn(50, 160) / 100f,
                circlePad = p.getBoolean("circle_pad", false),
                haptics = p.getBoolean("haptics", true),
                labelMapping = p.getString("gamepad_mapping", "position") == "label",
                dualDisplay = p.getBoolean("dual_display", true),
                dualScaling = if (p.getString("dual_scaling", "fill") == "fit") DualScaling.FIT else DualScaling.FILL,
                topOnSecondDisplay = p.getString("top_display", "main") == "second",
                dualControls = p.getBoolean("dual_controls", false),
                voxelAASamples = VoxelAntiAliasing.samples(p.getString("voxel_aa", "0")),
            )
        }
    }
}
