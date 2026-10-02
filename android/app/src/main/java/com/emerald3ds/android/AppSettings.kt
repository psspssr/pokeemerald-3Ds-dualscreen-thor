package com.emerald3ds.android

import android.content.Context
import androidx.preference.PreferenceManager

enum class ControlsVisibility { AUTO, ALWAYS, NEVER }

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
    val topOnSecondDisplay: Boolean,
    val dualControls: Boolean,
) {
    companion object {
        const val PORTRAIT_FILL = "fill"
        const val PORTRAIT_CONSOLE = "console"
        const val LANDSCAPE_SIDE_BY_SIDE = "side_by_side"
        const val LANDSCAPE_TOP_LARGE = "top_large"
        const val LANDSCAPE_TOP_ONLY = "top_only"
        const val LANDSCAPE_STACKED = "stacked"

        fun load(context: Context): AppSettings {
            PreferenceManager.setDefaultValues(context, R.xml.preferences, false)
            val p = PreferenceManager.getDefaultSharedPreferences(context)
            return AppSettings(
                portraitLayout = p.getString("layout_portrait", PORTRAIT_FILL) ?: PORTRAIT_FILL,
                landscapeLayout = p.getString("layout_landscape", LANDSCAPE_SIDE_BY_SIDE) ?: LANDSCAPE_SIDE_BY_SIDE,
                integerScaling = p.getBoolean("integer_scaling", false),
                linearFilter = p.getString("filter", "linear") != "nearest",
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
                topOnSecondDisplay = p.getString("top_display", "main") == "second",
                dualControls = p.getBoolean("dual_controls", false),
            )
        }
    }
}
