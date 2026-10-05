package com.emerald3ds.android

import android.app.Activity
import android.app.Presentation
import android.graphics.Color
import android.os.Bundle
import android.view.Display
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * The screen shown on a second display (the AYN Thor's bottom panel): native
 * window 1, plus the touch mapping when it shows the bottom screen.
 */
class GamePresentation(
    private val activity: Activity,
    display: Display,
    private val host: Host,
) : Presentation(activity, display), SurfaceHolder.Callback {

    interface Host {
        fun onSecondSurfaceChanged(width: Int, height: Int)
    }

    val touchView by lazy { ControlsOverlayView(context, InputHub.SRC_PRESENTATION, hasControls = false) }

    internal lateinit var surfaceView: SurfaceView
        private set

    var surfaceWidth = 0
        private set
    var surfaceHeight = 0
        private set

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // This window is a touch/display surface. Keep keyboard and gamepad
        // focus on the Activity, including while a finger uses this panel.
        // It also avoids API 30's native MOVE/FOCUS batching dead end.
        window?.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        val root = FrameLayout(context).apply { setBackgroundColor(Color.BLACK) }
        val surface = SurfaceView(context)
        surfaceView = surface
        surface.holder.addCallback(this)
        val match = ViewGroup.LayoutParams.MATCH_PARENT
        root.addView(surface, FrameLayout.LayoutParams(match, match))
        root.addView(touchView, FrameLayout.LayoutParams(match, match))
        setContentView(root)
        window?.let {
            it.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            WindowCompat.setDecorFitsSystemWindows(it, false)
            WindowInsetsControllerCompat(it, root).apply {
                hide(WindowInsetsCompat.Type.systemBars())
                systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {}

    fun setKeepScreenOn(enabled: Boolean) {
        if (enabled) window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        surfaceWidth = width
        surfaceHeight = height
        NativeBridge.setSurface(NativeBridge.WINDOW_SECOND, holder.surface)
        host.onSecondSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        touchView.releaseAll()
        surfaceWidth = 0
        surfaceHeight = 0
        NativeBridge.setSurface(NativeBridge.WINDOW_SECOND, null)
        host.onSecondSurfaceChanged(0, 0)
    }

    /* Explicitly routed keys still belong to the Activity. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean = activity.dispatchKeyEvent(event)

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
        activity.dispatchGenericMotionEvent(ev) || super.dispatchGenericMotionEvent(ev)
}
