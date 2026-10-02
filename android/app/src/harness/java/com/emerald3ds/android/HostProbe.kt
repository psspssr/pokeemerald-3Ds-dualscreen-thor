package com.emerald3ds.android

/** Native host observations, available only in the display test build. */
object HostProbe {
    init { check(NativeBridge.loaded) }
    @JvmStatic external fun snapshot(): IntArray
}
