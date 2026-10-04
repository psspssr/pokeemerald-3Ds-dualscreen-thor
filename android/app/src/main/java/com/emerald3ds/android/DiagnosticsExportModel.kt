package com.emerald3ds.android

import android.app.Application
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import java.io.OutputStream
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.concurrent.Executors

/** A manual export survives Activity recreation; neither the worker nor its result owns a View. */
class DiagnosticsExportModel(application: Application) : AndroidViewModel(application) {
    enum class Phase { IDLE, PICKING, WRITING, COMPLETE }
    enum class Result { EXPORTED, WRITE_FAILED, PICKER_FAILED }
    data class State(val phase: Phase = Phase.IDLE, val result: Result? = null)
    private val mutableState = MutableLiveData(State())
    val state: LiveData<State> = mutableState
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { task -> Thread(task, "diagnostics-export") }
    @Volatile private var cleared = false
    internal var openOutput: (Uri) -> OutputStream = { uri ->
        DiagnosticsDocuments.open(application, uri)
    }

    /** UI thread. Return null when another request already owns the picker/worker. */
    fun begin(): String? {
        if (mutableState.value?.phase != Phase.IDLE) return null
        mutableState.value = State(Phase.PICKING)
        val stamp = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss").withZone(ZoneOffset.UTC).format(Instant.now())
        return "emerald-thor-diagnostics-$stamp.json"
    }

    fun pickerFailed() {
        if (mutableState.value?.phase == Phase.PICKING)
            mutableState.value = State(Phase.COMPLETE, Result.PICKER_FAILED)
    }

    /** The Activity Result registry may restore a result after process recreation. */
    fun destination(uri: Uri?) {
        if (mutableState.value?.phase == Phase.WRITING) return
        if (uri == null) {
            mutableState.value = State()
            return
        }
        mutableState.value = State(Phase.WRITING)
        worker.execute {
            val result = try {
                val bytes = Diagnostics.report(getApplication())
                openOutput(uri).use { it.write(bytes) }
                Result.EXPORTED
            } catch (failure: Exception) {
                // Provider exceptions can contain private document paths.
                Diagnostics.record(Diagnostics.Event.EXPORT_FAILED, failure.javaClass.simpleName.take(80))
                Result.WRITE_FAILED
            }
            main.post { if (!cleared) mutableState.value = State(Phase.COMPLETE, result) }
        }
    }

    fun consumeResult() {
        if (mutableState.value?.phase == Phase.COMPLETE) mutableState.value = State()
    }

    override fun onCleared() {
        cleared = true
        // Finish an already-open document; never retain or call a destroyed UI.
        worker.shutdown()
    }
}
