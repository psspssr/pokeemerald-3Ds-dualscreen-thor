package com.emerald3ds.android

import android.view.Display

/** The activity's panel must be usable as well as the device being awake.
 * A second panel can keep Android interactive while this one is powered off. */
internal data class ScreenPowerState(
    val interactive: Boolean,
    val keyguardLocked: Boolean,
    val displayState: Int,
) {
    val permitsPlay: Boolean
        get() = interactive && !keyguardLocked && displayState == Display.STATE_ON
}
