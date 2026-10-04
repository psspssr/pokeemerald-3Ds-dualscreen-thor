package com.emerald3ds.android

import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Debug
import android.os.Process
import android.os.SystemClock
import android.view.Display
import androidx.preference.PreferenceManager
import org.json.JSONArray
import org.json.JSONObject
import java.io.InputStream
import java.time.Instant
import java.util.ArrayDeque
import kotlin.math.ceil

/** App-owned, bounded metadata only. Never reads logcat, game files or document contents. */
internal object Diagnostics {
    const val PREFERENCE = "diagnostics_recording"
    const val EVENT_LIMIT = 64
    const val MAX_REPORT_BYTES = 64 * 1024
    enum class Event { RECORDING_STARTED, GAME_CREATED, GAME_RESUMED, GAME_PAUSED, GAME_DESTROYED,
        SCREEN_POWER, LAYOUT, DISPLAY_CHANGED, DISPLAY_ERROR, GAME_ERROR, NATIVE_EXIT, BACKUP_FAILED, EXPORT_FAILED }
    private data class Entry(val uptimeMs: Long, val event: Event, val detail: String)
    private data class Layout(val uptimeMs: Long, val main: Int, val second: Int?, val topWindow: Int,
        val bottomWindow: Int, val top: IntArray?, val bottom: IntArray?) {
        fun json() = JSONObject().put("uptime_ms", uptimeMs).put("main_display_id", main)
            .put("secondary_display_id", second ?: JSONObject.NULL).put("top_window", topWindow)
            .put("bottom_window", bottomWindow).put("top_rect_xywh", top?.let { JSONArray(it.toList()) } ?: JSONObject.NULL)
            .put("bottom_rect_xywh", bottom?.let { JSONArray(it.toList()) } ?: JSONObject.NULL)
    }
    private val lock = Any()
    private val events = ArrayDeque<Entry>()
    @Volatile private var recording = false
    private var layout: Layout? = null
    private var lastActiveLayout: Layout? = null

    fun isRecording() = recording

    fun configure(context: Context) {
        applyRecording(PreferenceManager.getDefaultSharedPreferences(context).getBoolean(PREFERENCE, false))
    }

    fun setRecording(context: Context, enabled: Boolean) {
        PreferenceManager.getDefaultSharedPreferences(context).edit().putBoolean(PREFERENCE, enabled).apply()
        applyRecording(enabled)
    }

    private fun applyRecording(enabled: Boolean) {
        synchronized(lock) {
            if (recording != enabled) {
                events.clear()
                lastActiveLayout = null
                recording = enabled
                if (enabled) events.addLast(Entry(SystemClock.uptimeMillis(), Event.RECORDING_STARTED, ""))
            }
        }
        NativeBridge.setDiagnosticsRecording(enabled)
    }

    /** Callers pass fixed codes and numeric state, never exception messages or paths. */
    fun record(event: Event, detail: String = "") {
        if (!recording) return
        synchronized(lock) {
            if (!recording) return
            val bounded = detail.take(160)
            if (event == Event.LAYOUT && events.peekLast()?.let { it.event == event && it.detail == bounded } == true) return
            if (events.size == EVENT_LIMIT) events.removeFirst()
            events.addLast(Entry(SystemClock.uptimeMillis(), event, bounded))
        }
    }

    fun layout(value: ScreenLayout.Result, mainDisplay: Int, secondDisplay: Int?, active: Boolean) {
        fun rect(r: android.graphics.Rect?) = r?.let { intArrayOf(it.left, it.top, it.width(), it.height()) }
        val next = Layout(SystemClock.uptimeMillis(), mainDisplay, secondDisplay,
            value.topWindow, value.bottomWindow, rect(value.top), rect(value.bottom))
        synchronized(lock) {
            layout = next
            if (recording && active) lastActiveLayout = next
        }
        record(Event.LAYOUT, "top=${value.topWindow}:${value.top?.width()}x${value.top?.height()} bottom=${value.bottomWindow}:${value.bottom?.width()}x${value.bottom?.height()} main=$mainDisplay second=$secondDisplay")
    }

    /** Run on the export worker. Memory sampling and JSON formatting are manual-export work only. */
    fun report(context: Context): ByteArray {
        val root = JSONObject().put("schema_version", 1).put("captured_at_utc", Instant.now().toString())
            .put("uptime_ms", SystemClock.uptimeMillis()).put("recording_enabled", recording)
            .put("scope", "App/device/runtime metadata only. No saves, game data, document URIs, persistent device identifiers, input history or global logcat.")
        root.put("app", JSONObject().put("package", BuildConfig.APPLICATION_ID)
            .put("version_name", BuildConfig.VERSION_NAME.take(160)).put("version_code", BuildConfig.VERSION_CODE)
            .put("build_type", BuildConfig.BUILD_TYPE).put("host_harness", BuildConfig.HOST_HARNESS)
            .put("asset_engine_abi_hex", engineAbi(context) ?: JSONObject.NULL))
        root.put("device", JSONObject().put("manufacturer", Build.MANUFACTURER.take(80)).put("model", Build.MODEL.take(80))
            .put("android_release", Build.VERSION.RELEASE.take(40)).put("sdk", Build.VERSION.SDK_INT)
            .put("security_patch", Build.VERSION.SECURITY_PATCH.take(40)).put("process_64_bit", Process.is64Bit())
            .put("supported_abis", JSONArray(Build.SUPPORTED_ABIS.take(8).map { it.take(64) }))
            .put("supported_32_bit_abis", JSONArray(Build.SUPPORTED_32_BIT_ABIS.take(8).map { it.take(64) })))
        val displays = JSONArray()
        context.getSystemService(DisplayManager::class.java).displays.take(16).forEach { display ->
            val entry = JSONObject().put("id", display.displayId)
            try {
                val mode = display.mode
                entry.put("valid", display.isValid).put("state", displayState(display.state)).put("rotation", display.rotation)
                    .put("width_px", mode.physicalWidth).put("height_px", mode.physicalHeight)
                    .put("refresh_rate_hz", refreshRate(mode.refreshRate))
            } catch (_: RuntimeException) {
                entry.put("valid", false).put("state", "unavailable")
            }
            displays.put(entry)
        }
        root.put("displays", displays)
        synchronized(lock) {
            root.put("current_window_assignment", layout?.json() ?: JSONObject.NULL)
            root.put("last_recorded_active_assignment", lastActiveLayout?.json() ?: JSONObject.NULL)
            root.put("recent_app_events", JSONArray().also { list -> events.forEach {
                list.put(JSONObject().put("uptime_ms", it.uptimeMs).put("event", it.event.name.lowercase()).put("detail", it.detail))
            } })
        }
        val memory = Debug.MemoryInfo()
        try {
            Debug.getMemoryInfo(memory)
            val runtime = Runtime.getRuntime()
            root.put("memory", JSONObject().put("measured", true).put("scope", "current app process at export time")
                .put("total_pss_kib", memory.totalPss).put("private_dirty_kib", memory.totalPrivateDirty)
                .put("private_clean_kib", memory.totalPrivateClean).put("native_allocator_bytes", Debug.getNativeHeapAllocatedSize())
                .put("java_heap_used_bytes", runtime.totalMemory() - runtime.freeMemory()))
        } catch (_: RuntimeException) {
            root.put("memory", JSONObject().put("measured", false).put("reason", "system_measurement_unavailable"))
        }
        val native = NativeBridge.diagnostics()
        root.put("native", (native?.let(::nativeJson) ?: JSONObject().put("available", false).put("reason", "native_bridge_unavailable"))
            .put("library_loaded", NativeBridge.loaded).put("game_exit_status", NativeBridge.gameExitStatus ?: JSONObject.NULL))
        val bytes = (root.toString(2) + "\n").toByteArray(Charsets.UTF_8)
        check(bytes.size <= MAX_REPORT_BYTES) { "diagnostic report exceeded its size limit" }
        return bytes
    }

    private fun displayState(state: Int) = when (state) {
        Display.STATE_ON -> "on"; Display.STATE_OFF -> "off"; Display.STATE_DOZE -> "doze"
        Display.STATE_DOZE_SUSPEND -> "doze_suspend"; Display.STATE_ON_SUSPEND -> "on_suspend"
        else -> "unknown"
    }

    internal fun refreshRate(value: Float): Any = if (value.isFinite() && value > 0f) value.toDouble() else JSONObject.NULL

    private fun engineAbi(context: Context): String? = try {
        // Four generated build-metadata bytes from the APK, never a save or external data pack.
        context.assets.open("romfs/engine/abi.bin").use(::readEngineAbi)
    } catch (_: java.io.IOException) { null }

    internal fun readEngineAbi(input: InputStream): String? {
        // Match the build manifest's uint32 ID, not its little-endian byte dump.
        var value = 0
        repeat(4) { index ->
            val byte = input.read()
            if (byte < 0) return null
            value = value or (byte shl (index * 8))
        }
        if (input.read() != -1) return null
        return value.toUInt().toString(16).padStart(8, '0')
    }

    private fun statistics(values: List<Long>): Any {
        if (values.isEmpty()) return JSONObject.NULL
        val sorted = values.sorted()
        return JSONObject().put("samples", sorted.size).put("mean_ms", sorted.average() / 1_000_000.0)
            .put("p50_ms", sorted[(sorted.size - 1) / 2] / 1_000_000.0)
            .put("p95_ms", sorted[(ceil(sorted.size * 0.95).toInt() - 1).coerceAtLeast(0)] / 1_000_000.0)
            .put("max_ms", sorted.last() / 1_000_000.0)
    }

    internal fun nativeJson(snapshot: NativeBridge.DiagnosticSnapshot): JSONObject {
        val out = JSONObject().put("available", true)
        val identity = snapshot.identity
        out.put("renderer", if (identity.size == 5) JSONObject().put("backend", identity[0]).put("native_abi", identity[4])
            .put("identity_available", identity[1].isNotEmpty() && identity[2].isNotEmpty())
            .put("vendor", identity[1]).put("renderer", identity[2]).put("version", identity[3])
            else JSONObject().put("identity_available", false))
        val state = snapshot.state
        if (state.size == 16 && state[0] == 1) out.put("current_host_state", JSONObject().put("started", state[1] != 0)
            .put("state", when (state[2]) { 0 -> "running"; 1 -> "paused"; else -> "exiting" })
            .put("effective_speed", state[3]).put("top_window", state[4]).put("bottom_window", state[5])
            .put("top_rect_xywh", JSONArray(state.slice(6..9))).put("bottom_rect_xywh", JSONArray(state.slice(10..13))))
        val data = snapshot.samples
        val frames = data.getOrNull(3)?.toInt() ?: -1
        val errors = data.getOrNull(4)?.toInt() ?: -1
        if (data.size < 5 || data[0] != 1L || frames !in 0..240 || errors !in 0..16 || data.size != 5 + frames * 4 + errors * 3) {
            return out.put("presentation", JSONObject().put("measured", false).put("reason", "unsupported_native_snapshot"))
        }
        val work = (0 until frames).map { data[5 + it * 4 + 1] }
        val intervals = (0 until frames).map { data[5 + it * 4 + 2] }.filter { it > 0 }
        out.put("presentation", JSONObject().put("recording_enabled", data[1] != 0L).put("measured", frames > 0)
            .put("reason", if (frames > 0) "measured_recent_successful_presentations" else if (data[1] == 0L) "recording_disabled" else "no_successful_presentations_yet")
            .put("total_presentations_since_enable", data[2]).put("retained_presentations", frames)
            .put("single_surface_presentations", (0 until frames).count { data[5 + it * 4 + 3] == 1L })
            .put("dual_surface_presentations", (0 until frames).count { data[5 + it * 4 + 3] == 2L })
            .put("first_sample_monotonic_ns", if (frames > 0) data[5] else JSONObject.NULL)
            .put("last_sample_monotonic_ns", if (frames > 0) data[5 + (frames - 1) * 4] else JSONObject.NULL)
            .put("definition", "One sample per successful presentation call across one or two surfaces. Intervals exclude pause boundaries. Work is elapsed presentation-call time including CPU/driver waits, not GPU execution or simulation timing.")
            .put("work", statistics(work)).put("interval", statistics(intervals)))
        val errorList = JSONArray()
        for (i in 0 until errors) {
            val at = 5 + frames * 4 + i * 3
            errorList.put(JSONObject().put("site", when (data[at].toInt()) {
                1 -> "egl_init"; 2 -> "egl_surface"; 3 -> "egl_make_current"; 4 -> "egl_swap"; else -> "unknown"
            }).put("code", data[at + 1]).put("monotonic_ns", data[at + 2]))
        }
        out.put("recent_graphics_errors", errorList)
        return out
    }
}
