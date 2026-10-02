package com.emerald3ds.android

import android.content.Intent
import android.content.res.Configuration
import android.graphics.Rect
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.text.Html
import android.util.Log
import android.view.Display
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.addCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.IOException
import java.lang.ref.WeakReference

/**
 * The only game activity. It is never recreated (configChanges), extracts the
 * RomFS when the APK changed, asks for the data pack if missing, then starts
 * origin's main() once per process and feeds it surfaces, layout, input and
 * pause state.
 */
class GameActivity : AppCompatActivity(), SurfaceHolder.Callback, ControlsOverlayView.Listener,
    PhysicalInput.Callbacks, DisplayManager.DisplayListener, GamePresentation.Host {

    private enum class Phase { STARTING, EXTRACTING, NEED_PAK, ERROR, RUNNING }

    private val handler = Handler(Looper.getMainLooper())
    private lateinit var files: GameFiles
    private lateinit var settings: AppSettings
    private lateinit var overlay: ControlsOverlayView
    private lateinit var physical: PhysicalInput
    private lateinit var displayManager: DisplayManager

    private lateinit var root: FrameLayout
    private lateinit var statusPanel: View
    private lateinit var statusTitle: TextView
    private lateinit var statusBody: TextView
    private lateinit var statusProgress: ProgressBar
    private lateinit var buttonPrimary: MaterialButton
    private lateinit var buttonSave: MaterialButton

    private var phase = Phase.STARTING
    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var safe = Rect()
    private var bottomToggled = false
    internal var presentation: GamePresentation? = null
        private set
    private var dualActive = false
    private var resumed = false
    private var menuShown = false
    private var exiting = false

    private val importPak = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFromStatus(uri, GameFiles.Kind.PAK)
    }
    private val importSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFromStatus(uri, GameFiles.Kind.SAVE)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        current = WeakReference(this)
        NativeBridge.appContext = applicationContext
        files = GameFiles(this)
        files.ensureDirs()
        settings = AppSettings.load(this)
        setContentView(R.layout.activity_game)

        root = findViewById(R.id.root)
        statusPanel = findViewById(R.id.status_panel)
        statusTitle = findViewById(R.id.status_title)
        statusBody = findViewById(R.id.status_body)
        statusProgress = findViewById(R.id.status_progress)
        buttonPrimary = findViewById(R.id.button_primary)
        buttonSave = findViewById(R.id.button_save)
        findViewById<MaterialButton>(R.id.button_settings).setOnClickListener { openSettings() }
        findViewById<MaterialButton>(R.id.button_quit).setOnClickListener { exitProcess() }
        buttonSave.setOnClickListener { importSave.launch(arrayOf("*/*")) }

        findViewById<SurfaceView>(R.id.game_surface).holder.addCallback(this)
        overlay = ControlsOverlayView(this, InputHub.SRC_OVERLAY, hasControls = true)
        overlay.listener = this
        findViewById<FrameLayout>(R.id.overlay_container).addView(
            overlay, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        )
        physical = PhysicalInput(this)

        root.setOnApplyWindowInsetsListener { _, insets ->
            val cutout = insets.displayCutout
            safe = if (cutout != null)
                Rect(cutout.safeInsetLeft, cutout.safeInsetTop, cutout.safeInsetRight, cutout.safeInsetBottom)
            else Rect()
            relayout()
            insets
        }
        onBackPressedDispatcher.addCallback(this) { onBackKey() }

        displayManager = getSystemService(DisplayManager::class.java)
        displayManager.registerDisplayListener(this, handler)
        enterImmersive()
        proceed()
    }

    override fun onDestroy() {
        displayManager.unregisterDisplayListener(this)
        dismissPresentation()
        if (!exiting) handler.removeCallbacksAndMessages(null)
        if (current?.get() === this) current = null
        super.onDestroy()
    }

    override fun onStart() {
        super.onStart()
        updatePresentation()
    }

    override fun onStop() {
        dismissPresentation()
        super.onStop()
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        settings = AppSettings.load(this)
        physical.labelMapping = settings.labelMapping
        if (settings.keepScreenOn) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        enterImmersive()
        updatePresentation()
        relayout()
        if (phase == Phase.NEED_PAK) proceed()
        if (!menuShown) NativeBridge.setState(NativeBridge.STATE_RUNNING)
        updateInputEnabled()
    }

    override fun onPause() {
        resumed = false
        updateInputEnabled()
        NativeBridge.setState(NativeBridge.STATE_PAUSED)
        overlay.releaseAll()
        presentation?.touchView?.releaseAll()
        physical.clear()
        InputHub.clear()
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) enterImmersive()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        bottomToggled = false
        relayout()
    }

    private fun enterImmersive() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    // ── Start-up ─────────────────────────────────────────────────────────

    private fun proceed() {
        when {
            NativeBridge.isStarted() -> showGame()
            files.needsExtraction() -> extract()
            !NativeBridge.loaded -> showError(getString(R.string.native_missing))
            else -> {
                try {
                    files.applyPendingImports()
                    if (BuildConfig.HOST_HARNESS || files.pakFile.isFile || files.hasEmbeddedGameData()) startGame()
                    else showNeedPak()
                } catch (e: IOException) {
                    showError(getString(R.string.import_failed, e.message))
                }
            }
        }
    }

    private fun showStatus(title: String, body: CharSequence?, progress: Boolean, buttons: Boolean) {
        statusPanel.visibility = View.VISIBLE
        statusTitle.text = title
        statusBody.text = body ?: ""
        statusBody.visibility = if (body == null) View.GONE else View.VISIBLE
        statusProgress.visibility = if (progress) View.VISIBLE else View.GONE
        findViewById<View>(R.id.status_buttons).visibility = if (buttons) View.VISIBLE else View.GONE
    }

    private fun extract() {
        phase = Phase.EXTRACTING
        showStatus(getString(R.string.extracting), null, progress = true, buttons = false)
        statusProgress.isIndeterminate = true
        Thread({
            try {
                files.extractRomfs { done, total ->
                    runOnUiThread {
                        statusProgress.isIndeterminate = false
                        statusProgress.max = total
                        statusProgress.progress = done
                    }
                }
                runOnUiThread { proceed() }
            } catch (e: IOException) {
                Log.e(TAG, "RomFS extraction failed", e)
                runOnUiThread { showError(getString(R.string.extract_failed, e.message ?: e.toString())) }
            }
        }, "romfs-extract").start()
    }

    private fun showError(message: String) {
        phase = Phase.ERROR
        showStatus(message, null, progress = false, buttons = true)
        buttonPrimary.text = getString(R.string.retry)
        buttonPrimary.setOnClickListener { proceed() }
        buttonSave.visibility = View.GONE
    }

    private fun showNeedPak() {
        phase = Phase.NEED_PAK
        @Suppress("DEPRECATION")
        val body = Html.fromHtml(getString(R.string.pak_missing_body, files.pakFile.absolutePath))
        showStatus(getString(R.string.pak_missing_title), body, progress = false, buttons = true)
        buttonPrimary.text = getString(R.string.import_pak_button)
        buttonPrimary.setOnClickListener { importPak.launch(arrayOf("*/*")) }
        buttonSave.visibility = View.VISIBLE
        buttonPrimary.requestFocus()
    }

    private fun importFromStatus(uri: Uri, kind: GameFiles.Kind) {
        try {
            files.stageImport(uri, kind)
            if (!NativeBridge.isStarted()) files.applyPendingImports()
            Toast.makeText(this, R.string.import_done, Toast.LENGTH_SHORT).show()
        } catch (e: IOException) {
            Toast.makeText(this, getString(R.string.import_failed, e.message), Toast.LENGTH_LONG).show()
        }
        if (phase == Phase.NEED_PAK) proceed()
    }

    private fun startGame() {
        NativeBridge.init(files.romfsDir.absolutePath, files.sdmcDir.absolutePath)
        NativeBridge.setState(if (resumed && !menuShown) NativeBridge.STATE_RUNNING else NativeBridge.STATE_PAUSED)
        if (!NativeBridge.start()) {
            showError("The game thread could not be started.")
            return
        }
        showGame()
    }

    private fun showGame() {
        phase = Phase.RUNNING
        statusPanel.visibility = View.GONE
        relayout()
    }

    private fun updateInputEnabled() {
        val enabled = resumed && phase == Phase.RUNNING && !menuShown && !exiting
        overlay.inputEnabled = enabled
        presentation?.touchView?.inputEnabled = enabled
    }

    // ── Surfaces and layout ──────────────────────────────────────────────

    override fun surfaceCreated(holder: SurfaceHolder) {}

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        surfaceWidth = width
        surfaceHeight = height
        NativeBridge.setSurface(NativeBridge.WINDOW_MAIN, holder.surface)
        relayout()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceWidth = 0
        surfaceHeight = 0
        NativeBridge.setSurface(NativeBridge.WINDOW_MAIN, null)
    }

    override fun onSecondSurfaceChanged(width: Int, height: Int) = relayout()

    private fun relayout() {
        val w = if (surfaceWidth > 0) surfaceWidth else root.width
        val h = if (surfaceHeight > 0) surfaceHeight else root.height
        if (w <= 0 || h <= 0) return
        val p = presentation
        val dual = p != null && p.surfaceWidth > 0 && p.surfaceHeight > 0
        if (dual != dualActive) {
            dualActive = dual
            /* In dual-display mode the built-in controls are expected; touch brings ours back. */
            overlay.setAutoHidden(dual)
        }
        val allowed = when {
            settings.controlsVisibility == ControlsVisibility.NEVER -> false
            dual -> settings.dualControls
            else -> true
        }
        val controlsVisible = allowed && !overlay.autoHidden
        val layout = if (dual) {
            ScreenLayout.dual(w, h, safe, p!!.surfaceWidth, p.surfaceHeight, settings)
        } else {
            val reserve = if (controlsVisible) overlay.portraitReserve(settings.controlsScale) else 0
            ScreenLayout.single(w, h, safe, settings, reserve, controlsVisible, bottomToggled)
        }
        NativeBridge.setLayout(layout, settings.linearFilter, BACKGROUND)
        val showToggle = !dual && w > h && settings.landscapeLayout == AppSettings.LANDSCAPE_TOP_ONLY
        overlay.configure(
            settings, layout.topIn(NativeBridge.WINDOW_MAIN), layout.bottomIn(NativeBridge.WINDOW_MAIN),
            safe, showToggle, allowed,
        )
        if (dual) {
            p!!.touchView.configure(
                settings, layout.topIn(NativeBridge.WINDOW_SECOND), layout.bottomIn(NativeBridge.WINDOW_SECOND),
                Rect(), showToggle = false, allowed = false,
            )
        }
        updateInputEnabled()
        Log.d(TAG, "layout ${w}x$h dual=$dual top=${layout.top}@${layout.topWindow} bottom=${layout.bottom}@${layout.bottomWindow}")
    }

    override fun onControlsVisibilityChanged() {
        handler.post { relayout() }
    }

    override fun onToggleBottomScreen() {
        if (settings.landscapeLayout != AppSettings.LANDSCAPE_TOP_ONLY || dualActive) return
        bottomToggled = !bottomToggled
        relayout()
    }

    // ── Second display ───────────────────────────────────────────────────

    private fun currentDisplayId(): Int =
        if (Build.VERSION.SDK_INT >= 30) display?.displayId ?: Display.DEFAULT_DISPLAY
        else @Suppress("DEPRECATION") windowManager.defaultDisplay.displayId

    private fun pickSecondDisplay(): Display? {
        val own = currentDisplayId()
        return displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .firstOrNull { it.displayId != own && it.state != Display.STATE_OFF }
    }

    private fun updatePresentation() {
        val wanted = settings.dualDisplay && lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED) && !exiting
        val display = if (wanted) pickSecondDisplay() else null
        if (presentation?.display?.displayId == display?.displayId) return
        dismissPresentation()
        if (display != null) {
            val p = GamePresentation(this, display, this)
            try {
                p.setOnDismissListener {
                    if (presentation === p) {
                        presentation = null
                        p.touchView.releaseAll()
                        relayout()
                    }
                }
                presentation = p
                p.show()
                Log.i(TAG, "second display ${display.displayId}: ${display.name}")
            } catch (e: WindowManager.InvalidDisplayException) {
                presentation = null
                Log.w(TAG, "cannot use display ${display.displayId}", e)
            }
        }
        relayout()
    }

    private fun dismissPresentation() {
        val old = presentation
        presentation = null
        old?.let {
            it.touchView.releaseAll()
            it.dismiss()
        }
        if (::overlay.isInitialized) relayout()
    }

    override fun onDisplayAdded(displayId: Int) = updatePresentation()
    override fun onDisplayRemoved(displayId: Int) = updatePresentation()
    override fun onDisplayChanged(displayId: Int) = updatePresentation()

    // ── Input ────────────────────────────────────────────────────────────

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (phase == Phase.RUNNING && !menuShown && physical.onKey(event)) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean {
        if (phase == Phase.RUNNING && !menuShown && physical.onMotion(ev)) return true
        return super.dispatchGenericMotionEvent(ev)
    }

    override fun onPhysicalInput() {
        if (settings.controlsVisibility == ControlsVisibility.AUTO || dualActive) overlay.setAutoHidden(true)
    }

    override fun onMenuKey() = onBackKey()

    // ── Menu, settings, exit ─────────────────────────────────────────────

    private fun onBackKey() {
        if (phase != Phase.RUNNING) {
            if (phase != Phase.EXTRACTING) exitProcess()
            return
        }
        if (menuShown || exiting) return
        menuShown = true
        updateInputEnabled()
        NativeBridge.setState(NativeBridge.STATE_PAUSED)
        overlay.releaseAll()
        physical.clear()
        InputHub.clear()
        val items = arrayOf(getString(R.string.menu_resume), getString(R.string.menu_settings), getString(R.string.menu_quit))
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.menu_title)
            .setItems(items) { _, which ->
                when (which) {
                    1 -> openSettings()
                    2 -> quitGame()
                }
            }
            .setOnDismissListener {
                menuShown = false
                updateInputEnabled()
                if (resumed && !exiting) NativeBridge.setState(NativeBridge.STATE_RUNNING)
                enterImmersive()
            }
            .show()
    }

    private fun openSettings() {
        startActivity(Intent(this, SettingsActivity::class.java))
    }

    private fun quitGame() {
        if (!NativeBridge.isStarted()) {
            exitProcess()
            return
        }
        exiting = true
        NativeBridge.setState(NativeBridge.STATE_EXITING)
        /* main() normally returns within a frame; do not hang if it does not. */
        handler.postDelayed({ exitProcess() }, EXIT_TIMEOUT_MS)
    }

    /** The game cannot be restarted inside this process: end it. */
    fun exitProcess() {
        if (exiting && isFinishing) return
        exiting = true
        dismissPresentation()
        finishAndRemoveTask()
        handler.postDelayed({ Process.killProcess(Process.myPid()) }, KILL_DELAY_MS)
    }

    companion object {
        private const val TAG = "Emerald"
        private const val BACKGROUND = 0x000000
        private const val EXIT_TIMEOUT_MS = 3000L
        private const val KILL_DELAY_MS = 300L

        private var current: WeakReference<GameActivity>? = null

        fun onNativeGameExit(status: Int) {
            val activity = current?.get()
            Log.i(TAG, "native game exit $status, activity=${activity != null}")
            if (activity != null && !activity.isDestroyed) activity.exitProcess()
            else Process.killProcess(Process.myPid())
        }
    }
}
