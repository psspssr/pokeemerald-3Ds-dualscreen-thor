package com.emerald3ds.android

import android.app.Dialog
import android.view.View
import android.widget.ListView
import androidx.appcompat.app.AlertDialog

/** A dialog owns its input window. Observe releases before a focused child
 * can consume them, then preserve the dialog's normal navigation/actions. */
internal fun observePausedGameInput(dialog: Dialog) {
    val window = dialog.window ?: return
    var listInitialized = false
    MenuNavigation.install(window) {
        val alert = dialog as? AlertDialog
        val list = alert?.listView
        val focused = window.currentFocus
        if ((list == null || listInitialized) && focused != null && focused.isShown && focused.isEnabled && focused.isFocusable &&
            (focused !is ListView || focused.selectedItemPosition >= 0)) false
        else {
            if (list != null && list.isShown && list.adapter != null && list.count > 0) {
                val selected = list.selectedItemPosition.takeIf { it in 0 until list.count && list.adapter.isEnabled(it) }
                    ?: list.checkedItemPosition.takeIf { it in 0 until list.count && list.adapter.isEnabled(it) }
                    ?: (0 until list.count).firstOrNull { list.adapter.isEnabled(it) }
                if (selected == null) false
                else if (list.requestFocusFromTouch()) {
                    list.setSelection(selected)
                    listInitialized = true
                    true
                } else false
            } else {
                val target = listOf(AlertDialog.BUTTON_NEGATIVE, AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEUTRAL)
                    .mapNotNull { alert?.getButton(it) }.firstOrNull { it.visibility == View.VISIBLE && it.isEnabled }
                target?.requestFocusFromTouch() == true
            }
        }
    }
}
