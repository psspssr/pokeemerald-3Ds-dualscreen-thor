package com.emerald3ds.android

import android.os.Handler
import android.os.Looper
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import java.util.concurrent.Executors

/** Stable IDs and state codes shared with the native event catalogue. */
internal enum class MysteryEventStatus(val code: Int, val label: Int) {
    AVAILABLE(0, R.string.mystery_events_available),
    UNLOCKED(1, R.string.mystery_events_unlocked),
    COMPLETED(2, R.string.mystery_events_completed),
    NO_GAME(3, R.string.mystery_events_no_game),
    BUSY(4, R.string.mystery_events_busy),
    BAG_FULL(5, R.string.mystery_events_bag_full),
    PARTY_FULL(6, R.string.mystery_events_party_full),
    PREREQUISITE(7, R.string.mystery_events_prerequisite),
    DECOR_FULL(8, R.string.mystery_events_decor_full),
    UNAVAILABLE(-1, R.string.mystery_events_unavailable);

    companion object {
        fun fromCode(code: Int?) = entries.firstOrNull { it.code == code } ?: UNAVAILABLE
    }
}

internal data class MysteryEvent(
    val id: Int, val title: Int, val summary: Int, val description: Int,
    val prerequisite: Int = R.string.mystery_events_need_pokedex,
) {
    val key: String get() = "mystery_event_$id"

    companion object {
        val catalogue = listOf(
            MysteryEvent(0, R.string.mystery_eon_title, R.string.mystery_eon_summary, R.string.mystery_eon_description),
            MysteryEvent(1, R.string.mystery_mystic_title, R.string.mystery_mystic_summary, R.string.mystery_mystic_description),
            MysteryEvent(2, R.string.mystery_aurora_title, R.string.mystery_aurora_summary, R.string.mystery_aurora_description),
            MysteryEvent(3, R.string.mystery_sea_map_title, R.string.mystery_sea_map_summary, R.string.mystery_sea_map_description),
            MysteryEvent(4, R.string.mystery_jirachi_title, R.string.mystery_jirachi_summary, R.string.mystery_jirachi_description),
            MysteryEvent(5, R.string.mystery_celebi_title, R.string.mystery_celebi_summary, R.string.mystery_celebi_description),
            MysteryEvent(6, R.string.mystery_regi_dolls_title, R.string.mystery_regi_dolls_summary, R.string.mystery_regi_dolls_description),
        )
    }
}

/** These blocking operations queue work to the paused game thread. */
internal interface MysteryEventsBackend {
    fun read(): IntArray
    fun activate(event: Int): Int
}

internal object NativeMysteryEvents : MysteryEventsBackend {
    override fun read() = NativeBridge.mysteryEvents()
    override fun activate(event: Int) = NativeBridge.activateMysteryEvent(event)
}

internal class MysteryEventsModel(private val backend: MysteryEventsBackend) : ViewModel() {
    data class State(
        val statuses: List<MysteryEventStatus> = emptyList(),
        val busy: Boolean = false,
        val event: Int? = null,
        val result: Int? = null,
    ) {
        fun status(id: Int) = statuses.getOrNull(id) ?: MysteryEventStatus.UNAVAILABLE
        fun canActivate(id: Int) = !busy && status(id) == MysteryEventStatus.AVAILABLE
    }

    private val mutableState = MutableLiveData(State())
    val state: LiveData<State> = mutableState
    @Volatile private var cleared = false

    fun refresh() = submit(null)

    fun activate(event: Int) {
        if (MysteryEvent.catalogue.none { it.id == event } || mutableState.value?.canActivate(event) != true) return
        submit(event)
    }

    private fun submit(event: Int?) {
        val previous = mutableState.value ?: State()
        if (previous.busy) return
        mutableState.value = previous.copy(busy = true, event = event)
        worker.execute {
            // Another menu's request may have held the shared queue while
            // this one was left. Do not begin its deferred action afterwards.
            if (cleared) return@execute
            // A new status read follows every attempted activation. A changed
            // story/save state is rechecked by native code before any mutation.
            val result = if (event == null) previous.result else try { backend.activate(event) }
                catch (_: Exception) { RESULT_FAILED }
            val statuses = try {
                val raw = backend.read()
                MysteryEvent.catalogue.map { MysteryEventStatus.fromCode(raw.getOrNull(it.id)) }
            } catch (_: Exception) {
                emptyList()
            }
            main.post {
                if (!cleared) mutableState.value = State(statuses, event = event, result = result)
            }
        }
    }

    fun consumeResult() {
        mutableState.value?.let { if (it.result != null) mutableState.value = it.copy(result = null) }
    }

    override fun onCleared() {
        // Skip work still waiting in the shared queue. A native call already
        // started is allowed to return its exact result, without a UI callback.
        cleared = true
    }

    class Factory(private val backend: MysteryEventsBackend = NativeMysteryEvents) : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            require(modelClass == MysteryEventsModel::class.java)
            @Suppress("UNCHECKED_CAST")
            return MysteryEventsModel(backend) as T
        }
    }

    companion object {
        const val RESULT_ACTIVATED = 0
        const val RESULT_ALREADY = 1
        const val RESULT_NO_GAME = 2
        const val RESULT_BUSY = 3
        const val RESULT_NO_SPACE = 4
        const val RESULT_INVALID = 5
        const val RESULT_FAILED = 6
        const val RESULT_TIMEOUT = 7
        private val worker = Executors.newSingleThreadExecutor { task -> Thread(task, "mystery-events") }
        private val main = Handler(Looper.getMainLooper())
    }
}
