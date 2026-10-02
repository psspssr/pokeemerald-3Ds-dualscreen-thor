package com.emerald3ds.android

import android.app.Application
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import java.io.IOException
import java.util.concurrent.Executors

/**
 * File work survives configuration changes without retaining an Activity.
 * One process-wide queue keeps extraction and staged-import writes from
 * racing between the game and Settings, including a destroyed/reopened UI.
 */
class GameFilesModel(application: Application) : AndroidViewModel(application) {
    enum class Action { PREPARE, IMPORT_PAK, IMPORT_SAVE, EXPORT_SAVE }
    data class State(val action: Action, val busy: Boolean, val ready: Boolean = false,
                     val error: String? = null, val done: Int = 0, val total: Int = 0)

    val files = GameFiles(application)
    private val mutableState = MutableLiveData<State?>(null)
    val state: LiveData<State?> = mutableState
    @Volatile private var cleared = false

    fun prepare() = submit(Action.PREPARE) {
        files.ensureDirs()
        // Check after acquiring the shared queue: another Activity may have
        // finished extraction while this request was waiting.
        if (files.needsExtraction()) files.extractRomfs { done, total ->
            publish(State(Action.PREPARE, true, done = done, total = total))
        }
        files.applyPendingImports()
        BuildConfig.HOST_HARNESS || files.pakFile.isFile || files.hasEmbeddedGameData()
    }

    fun importFile(uri: Uri, kind: GameFiles.Kind) = submit(
        if (kind == GameFiles.Kind.PAK) Action.IMPORT_PAK else Action.IMPORT_SAVE
    ) {
        // Applying is reserved for prepare(), before the native thread starts.
        files.stageImport(uri, kind)
        false
    }

    fun exportSave(uri: Uri) = submit(Action.EXPORT_SAVE) {
        if (!NativeBridge.awaitPaused()) throw IOException("The game is still saving. Return to Settings and retry.")
        files.exportSave(uri)
        false
    }

    /** UI thread only. An unconsumed result also blocks overlapping requests. */
    internal fun submit(action: Action, work: () -> Boolean) {
        if (mutableState.value != null) return
        if (action == Action.EXPORT_SAVE) {
            mutablePauseHolds.value = (mutablePauseHolds.value ?: 0) + 1
            NativeBridge.setState(NativeBridge.STATE_PAUSED)
        }
        mutableState.value = State(action, true)
        worker.execute {
            val result = try { State(action, false, ready = work()) }
            catch (e: Exception) { State(action, false, error = e.message ?: e.javaClass.simpleName) }
            main.post {
                // A slow document provider may outlive Settings. Keep the
                // game paused until its save has been copied completely.
                if (action == Action.EXPORT_SAVE)
                    mutablePauseHolds.value = (mutablePauseHolds.value ?: 1) - 1
                if (!cleared) mutableState.value = result
            }
        }
    }

    fun consumeResult() {
        if (mutableState.value?.busy == false) mutableState.value = null
    }

    private fun publish(value: State) {
        main.post { if (!cleared) mutableState.value = value }
    }

    override fun onCleared() {
        // Let an in-flight copy finish atomically; discard callbacks to a UI
        // that has gone away. A new model's requests run after this copy.
        cleared = true
    }

    companion object {
        private val worker = Executors.newSingleThreadExecutor { task -> Thread(task, "game-files") }
        private val main = Handler(Looper.getMainLooper())
        private val mutablePauseHolds = MutableLiveData(0)
        val pauseHolds: LiveData<Int> = mutablePauseHolds
        val exportPending: Boolean get() = (mutablePauseHolds.value ?: 0) > 0
    }
}
