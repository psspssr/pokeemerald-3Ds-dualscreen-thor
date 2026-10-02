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

    lateinit var touchView: ControlsOverlayView
        private set

    var surfaceWidth = 0
        private set
    var surfaceHeight = 0
        private set

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val root = FrameLayout(context).apply { setBackgroundColor(Color.BLACK) }
        val surface = SurfaceView(context)
        surface.holder.addCallback(this)
        touchView = ControlsOverlayView(context, InputHub.SRC_PRESENTATION, hasControls = false)
        val match = ViewGroup.LayoutParams.MATCH_PARENT
        root.addView(surface, FrameLayout.LayoutParams(match, match))
        root.addView(touchView, FrameLayout.LayoutParams(match, match))
        setContentView(root)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {}

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        surfaceWidth = width
        surfaceHeight = height
        NativeBridge.setSurface(NativeBridge.WINDOW_SECOND, holder.surface)
        host.onSecondSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceWidth = 0
        surfaceHeight = 0
        NativeBridge.setSurface(NativeBridge.WINDOW_SECOND, null)
        host.onSecondSurfaceChanged(0, 0)
    }

    /* Touching this display can move key focus here; the activity owns input. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean = activity.dispatchKeyEvent(event)

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
        activity.dispatchGenericMotionEvent(ev) || super.dispatchGenericMotionEvent(ev)
}
