package com.emerald3ds.android

import android.graphics.Rect
import kotlin.math.floor
import kotlin.math.min

/**
 * Where the two 3DS screens go, in window pixels. The top screen is 400x240
 * (5:3), the bottom one 320x240 (4:3). Phone layouts and dual-display Fit
 * preserve those proportions; dual-display Fill stretches the full image.
 */
object ScreenLayout {
    class Result(
        val top: Rect?,
        val bottom: Rect?,
        val topWindow: Int = NativeBridge.WINDOW_MAIN,
        val bottomWindow: Int = NativeBridge.WINDOW_MAIN,
        /** Native menu content uses its whole area only in an active dual Fill layout. */
        val expandBottomMenus: Boolean = false,
    ) {
        fun topIn(window: Int): Rect? = if (topWindow == window) top else null
        fun bottomIn(window: Int): Rect? = if (bottomWindow == window) bottom else null
    }

    private const val TW = CtrKeys.TOP_WIDTH.toFloat()
    private const val BW = CtrKeys.BOTTOM_WIDTH.toFloat()
    private const val H = CtrKeys.TOP_HEIGHT.toFloat()

    /** Integer scaling rounds down, but never below 1x. */
    private fun scale(s: Float, integer: Boolean): Float = if (integer && s >= 1f) floor(s) else s

    private fun rect(x: Float, y: Float, w: Float, h: Float): Rect {
        val l = Math.round(x)
        val t = Math.round(y)
        return Rect(l, t, l + Math.round(w), t + Math.round(h))
    }

    /** One screen of size cw x ch, as large as fits, centred in area. */
    fun fitCentered(area: Rect, cw: Float, ch: Float, integer: Boolean): Rect {
        val s = scale(min(area.width() / cw, area.height() / ch), integer)
        return rect(area.left + (area.width() - cw * s) / 2f, area.top + (area.height() - ch * s) / 2f, cw * s, ch * s)
    }

    /**
     * Both screens in one window. [safe] excludes display cutouts;
     * [controlsReserve] is the height kept free below the screens in portrait
     * for the on-screen controls.
     */
    fun single(
        width: Int, height: Int, safe: Rect, settings: AppSettings,
        controlsReserve: Int, controlsVisible: Boolean, bottomToggled: Boolean,
    ): Result {
        val area = Rect(safe.left, safe.top, width - safe.right, height - safe.bottom)
        if (area.width() <= 0 || area.height() <= 0) return Result(null, null)
        val aw = area.width().toFloat()
        val ah = area.height().toFloat()
        val integer = settings.integerScaling
        if (height >= width) {
            val room = (ah - controlsReserve).coerceAtLeast(ah * 0.5f)
            if (settings.portraitLayout == AppSettings.PORTRAIT_CONSOLE) {
                val s = scale(min(aw / TW, room / (2 * H)), integer)
                val top = rect(area.left + (aw - TW * s) / 2f, area.top.toFloat(), TW * s, H * s)
                val bottom = rect(area.left + (aw - BW * s) / 2f, top.bottom.toFloat(), BW * s, H * s)
                return Result(top, bottom)
            }
            var st = aw / TW
            var sb = aw / BW
            val k = min(1f, room / (H * (st + sb)))
            st = scale(st * k, integer)
            sb = scale(sb * k, integer)
            if (integer) {
                while (H * (st + sb) > room && sb > 1f) {
                    if (sb >= st) sb -= 1f else st -= 1f
                }
            }
            val top = rect(area.left + (aw - TW * st) / 2f, area.top.toFloat(), TW * st, H * st)
            val bottom = rect(area.left + (aw - BW * sb) / 2f, top.bottom.toFloat(), BW * sb, H * sb)
            return Result(top, bottom)
        }
        return when (settings.landscapeLayout) {
            AppSettings.LANDSCAPE_TOP_LARGE -> {
                val top = fitCentered(area, TW, H, integer)
                val sb = scale(min(aw / BW, ah * 0.42f / H), integer)
                val bottom = rect(area.right - BW * sb, area.top.toFloat(), BW * sb, H * sb)
                Result(top, bottom)
            }
            AppSettings.LANDSCAPE_TOP_ONLY ->
                if (bottomToggled) Result(null, fitCentered(area, BW, H, integer))
                else Result(fitCentered(area, TW, H, integer), null)
            AppSettings.LANDSCAPE_STACKED -> {
                val s = scale(min(aw / TW, ah / (2 * H)), integer)
                val y = area.top + (ah - 2 * H * s) / 2f
                val top = rect(area.left + (aw - TW * s) / 2f, y, TW * s, H * s)
                Result(top, rect(area.left + (aw - BW * s) / 2f, top.bottom.toFloat(), BW * s, H * s))
            }
            else -> {
                val s = scale(min(aw / (TW + BW), ah / H), integer)
                val x = area.left + (aw - (TW + BW) * s) / 2f
                /* With controls, keep the strip under the screens free for them. */
                val y = if (controlsVisible) area.top.toFloat() else area.top + (ah - H * s) / 2f
                val top = rect(x, y, TW * s, H * s)
                Result(top, rect(top.right.toFloat(), y, BW * s, H * s))
            }
        }
    }

    /** One screen per window: the main window and a second display. */
    fun dual(
        mainWidth: Int, mainHeight: Int, mainSafe: Rect, secondWidth: Int, secondHeight: Int,
        settings: AppSettings,
    ): Result {
        val mainArea = Rect(mainSafe.left, mainSafe.top, mainWidth - mainSafe.right, mainHeight - mainSafe.bottom)
        val secondArea = Rect(0, 0, secondWidth, secondHeight)
        val integer = settings.integerScaling
        val expandMenus = settings.dualScaling == DualScaling.FILL &&
            mainArea.width() > 0 && mainArea.height() > 0 && secondWidth > 0 && secondHeight > 0
        fun screen(area: Rect, width: Float): Rect =
            if (settings.dualScaling == DualScaling.FILL) Rect(area)
            else fitCentered(area, width, H, integer)
        return if (settings.topOnSecondDisplay) {
            Result(
                screen(secondArea, TW), screen(mainArea, BW),
                NativeBridge.WINDOW_SECOND, NativeBridge.WINDOW_MAIN,
                expandBottomMenus = expandMenus,
            )
        } else {
            Result(
                screen(mainArea, TW), screen(secondArea, BW),
                NativeBridge.WINDOW_MAIN, NativeBridge.WINDOW_SECOND,
                expandBottomMenus = expandMenus,
            )
        }
    }
}
