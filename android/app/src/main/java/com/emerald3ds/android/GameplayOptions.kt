package com.emerald3ds.android

import android.content.Context
import androidx.preference.PreferenceManager

/** Optional rules are app preferences, never additions to an Emerald save. */
data class GameplayOptions(
    val fastForwardEnabled: Boolean = false,
    val fastForwardSpeed: Int = 4,
    val shinyMultiplier: Int = 1,
    val sharedExperience: Boolean = false,
    val saveBackups: Boolean = false,
    val protectShinies: Boolean = false,
) {
    companion object {
        fun load(context: Context): GameplayOptions {
            val prefs = PreferenceManager.getDefaultSharedPreferences(context)
            return GameplayOptions(
                fastForwardEnabled = prefs.getBoolean("qol_fast_forward", false),
                fastForwardSpeed = prefs.getString("qol_speed", "4")?.toIntOrNull()
                    ?.takeIf { it == 2 || it == 4 || it == 8 } ?: 4,
                shinyMultiplier = prefs.getString("qol_shiny", "1")?.toIntOrNull()
                    ?.takeIf { it in setOf(1, 2, 4, 8, 16, 32, 64) } ?: 1,
                sharedExperience = prefs.getBoolean("qol_shared_exp", false),
                saveBackups = prefs.getBoolean("qol_backups", false),
                protectShinies = prefs.getBoolean("qol_protect_shinies", false),
            )
        }
    }
}

/** Runtime activation is intentionally not persisted across app launches. */
class FastForwardState(private val changed: () -> Unit) {
    var options = GameplayOptions()
        private set
    var toggled = false
        private set
    private var held = false
    val speed: Int get() = if (options.fastForwardEnabled && (toggled || held)) options.fastForwardSpeed else 1

    fun configure(value: GameplayOptions) {
        options = value
        if (!value.fastForwardEnabled) { toggled = false; held = false }
        changed()
    }

    fun toggle() {
        if (!options.fastForwardEnabled) return
        toggled = !toggled
        changed()
    }

    fun hold(value: Boolean) {
        val next = value && options.fastForwardEnabled
        if (held == next) return
        held = next
        changed()
    }
}
