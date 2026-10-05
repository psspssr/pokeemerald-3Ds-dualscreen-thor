package com.emerald3ds.android

import android.app.Dialog
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Window

/** A dialog owns its input window. Observe releases before a focused child
 * can consume them, then preserve the dialog's normal navigation/actions. */
internal fun observePausedGameInput(dialog: Dialog) {
    val window = dialog.window ?: return
    val previous = window.callback
    if (previous !is PausedGameInputCallback)
        window.callback = PausedGameInputCallback(previous)
}

private class PausedGameInputCallback(private val previous: Window.Callback) : Window.Callback by previous {
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        GameActivity.observePausedKeyEvent(event)
        return previous.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        GameActivity.observePausedMotionEvent(event)
        return previous.dispatchGenericMotionEvent(event)
    }
}
