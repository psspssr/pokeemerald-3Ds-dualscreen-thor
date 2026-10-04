package com.emerald3ds.android

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.Log
import android.view.Surface
import android.widget.Toast

/**
 * JNI surface of libemerald.so (android/host/src/jni_bridge.c). Every native
 * setter is called on the UI thread; awaitPaused is called by the file worker.
 * onGameExit and vibrate are called by native code from the game thread.
 */
object NativeBridge {
    private const val TAG = "Emerald"

    const val STATE_RUNNING = 0
    const val STATE_PAUSED = 1
    const val STATE_EXITING = 2

    /** Index of the window on a second display (ctr_host.h). */
    const val WINDOW_MAIN = 0
    const val WINDOW_SECOND = 1

    val loaded: Boolean = try {
        if (!BuildConfig.HOST_HARNESS) System.loadLibrary("emeraldboot")
        System.loadLibrary("emerald")
        true
    } catch (e: UnsatisfiedLinkError) {
        Log.e(TAG, "libemerald.so not loaded", e)
        false
    }

    @Volatile
    var appContext: Context? = null

    @Volatile
    var gameExitStatus: Int? = null
        private set

    @JvmStatic private external fun nativeInit(romfsDir: String, sdmcDir: String)
    @JvmStatic private external fun nativeStart(): Boolean
    @JvmStatic private external fun nativeIsStarted(): Boolean
    @JvmStatic private external fun nativeSetSurface(surface: Surface?)
    @JvmStatic private external fun nativeSetSurfaceAt(index: Int, surface: Surface?)
    @JvmStatic private external fun nativeSetLayout(
        top: IntArray?, bottom: IntArray?, filter: Int, background: Int, topWindow: Int, bottomWindow: Int,
        expandBottomMenus: Boolean, voxelAASamples: Int,
    )
    @JvmStatic private external fun nativeVoxelAACapabilities(): Int
    @JvmStatic private external fun nativeSetInput(keys: Int, circleX: Int, circleY: Int, touchX: Int, touchY: Int)
    @JvmStatic private external fun nativeSetState(state: Int)
    @JvmStatic private external fun nativeAwaitPaused(timeoutMs: Int): Boolean
    @JvmStatic private external fun nativeSetGameplayOptions(speed: Int, shiny: Int, shared: Boolean, backups: Boolean, protect: Boolean)
    @JvmStatic private external fun nativeAnswerShinyFlee(request: Int, allow: Boolean)
    @JvmStatic private external fun nativeIsShinyFleePending(request: Int): Boolean
    @JvmStatic private external fun nativeTestShinyFlee(): Boolean
    @JvmStatic private external fun nativeMysteryEvents(): IntArray
    @JvmStatic private external fun nativeActivateMysteryEvent(event: Int): Int
    @JvmStatic private external fun nativeSetDiagnosticsRecording(enabled: Boolean)
    @JvmStatic private external fun nativeDiagnosticsIdentity(): Array<String>
    @JvmStatic private external fun nativeDiagnosticsSamples(): LongArray
    @JvmStatic private external fun nativeDiagnosticsState(): IntArray

    internal data class DiagnosticSnapshot(val identity: Array<String>, val samples: LongArray, val state: IntArray)

    internal fun setDiagnosticsRecording(enabled: Boolean): Boolean = try {
        if (loaded) nativeSetDiagnosticsRecording(enabled)
        loaded
    } catch (_: UnsatisfiedLinkError) { false }

    internal fun diagnostics(): DiagnosticSnapshot? = try {
        if (!loaded) null else DiagnosticSnapshot(nativeDiagnosticsIdentity(), nativeDiagnosticsSamples(), nativeDiagnosticsState())
    } catch (_: UnsatisfiedLinkError) { null }

    fun init(romfsDir: String, sdmcDir: String) {
        if (loaded) nativeInit(romfsDir, sdmcDir)
    }

    fun start(): Boolean = loaded && nativeStart()

    fun isStarted(): Boolean = loaded && nativeIsStarted()

    fun setSurface(index: Int, surface: Surface?) {
        if (!loaded) return
        if (index == WINDOW_MAIN) nativeSetSurface(surface) else nativeSetSurfaceAt(index, surface)
    }

    fun setLayout(layout: ScreenLayout.Result, linearFilter: Boolean, background: Int, voxelAASamples: Int = 0) {
        if (!loaded) return
        nativeSetLayout(
            layout.top?.let { intArrayOf(it.left, it.top, it.width(), it.height()) },
            layout.bottom?.let { intArrayOf(it.left, it.top, it.width(), it.height()) },
            if (linearFilter) 1 else 0, background and 0xFFFFFF, layout.topWindow, layout.bottomWindow,
            layout.expandBottomMenus, voxelAASamples,
        )
    }

    /** Cached renderer capabilities only; this never queries GL on the UI thread. */
    internal fun voxelAACapabilities(): Int = try {
        if (loaded) nativeVoxelAACapabilities() else -1
    } catch (_: UnsatisfiedLinkError) { -1 }

    fun setInput(keys: Int, circleX: Int, circleY: Int, touchX: Int, touchY: Int) {
        if (loaded) nativeSetInput(keys, circleX, circleY, touchX, touchY)
    }

    fun setState(state: Int) {
        if (loaded) nativeSetState(state)
    }

    fun setGameplayOptions(speed: Int, options: GameplayOptions) {
        if (loaded) nativeSetGameplayOptions(speed, options.shinyMultiplier,
            options.sharedExperience, options.saveBackups, options.protectShinies)
    }

    fun answerShinyFlee(request: Int, allow: Boolean) {
        if (loaded) nativeAnswerShinyFlee(request, allow)
    }

    fun isShinyFleePending(request: Int): Boolean = loaded && nativeIsShinyFleePending(request)

    /** Mystery-events worker only: native code services these at its paused frame boundary. */
    internal fun mysteryEvents(): IntArray = if (loaded) nativeMysteryEvents() else intArrayOf()

    internal fun activateMysteryEvent(event: Int): Int =
        if (loaded) nativeActivateMysteryEvent(event) else MysteryEventsModel.RESULT_NO_GAME

    internal fun testShinyFleeRoundTrip(): Boolean {
        check(BuildConfig.HOST_HARNESS) { "Only available in the isolated display test app" }
        return nativeTestShinyFlee()
    }

    @JvmStatic
    fun onShinyFleePrompt(request: Int) {
        Handler(Looper.getMainLooper()).post { GameActivity.onShinyFleePrompt(request) }
    }

    @JvmStatic
    @Suppress("UNUSED_PARAMETER")
    fun onBackupFailure(status: Int) {
        Diagnostics.record(Diagnostics.Event.BACKUP_FAILED, "status=$status")
        Handler(Looper.getMainLooper()).post {
            appContext?.let { Toast.makeText(it, R.string.backup_failed, Toast.LENGTH_LONG).show() }
        }
    }

    /** File worker only: wait for the game to finish its current frame/save. */
    fun awaitPaused(timeoutMs: Int = 2000): Boolean = gameExitStatus != null || !loaded || nativeAwaitPaused(timeoutMs)

    @JvmStatic
    fun onGameExit(status: Int) {
        gameExitStatus = status
        Diagnostics.record(Diagnostics.Event.NATIVE_EXIT, "status=$status")
        Log.i(TAG, "game exited ($status)")
        Handler(Looper.getMainLooper()).post { GameActivity.onNativeGameExit(status) }
    }

    @JvmStatic
    fun vibrate(milliseconds: Int) {
        val vibrator = appContext?.getSystemService(Vibrator::class.java) ?: return
        if (vibrator.hasVibrator())
            vibrator.vibrate(VibrationEffect.createOneShot(milliseconds.toLong(), VibrationEffect.DEFAULT_AMPLITUDE))
    }
}
