package com.emerald3ds.android

import android.app.Application
import android.content.ContentProvider
import android.content.ContentResolver
import android.content.ContentValues
import android.content.ContextWrapper
import android.database.Cursor
import android.net.Uri
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.system.Os
import android.system.ErrnoException
import android.system.OsConstants
import android.util.Log
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

@RunWith(AndroidJUnit4::class)
class DiagnosticsTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val app get() = inst.targetContext.applicationContext as Application
    private val stores = mutableListOf<ViewModelStore>()
    private val temporary = mutableListOf<File>()

    @Before fun prepare() {
        check(BuildConfig.HOST_HARNESS)
        inst.runOnMainSync { Diagnostics.setRecording(app, false) }
    }

    @After fun cleanup() {
        inst.runOnMainSync {
            Diagnostics.setRecording(app, false)
            stores.forEach { it.clear() }
        }
        temporary.forEach { it.deleteRecursively() }
    }

    private fun model(): DiagnosticsExportModel {
        val store = ViewModelStore().also(stores::add)
        return ViewModelProvider(store, ViewModelProvider.AndroidViewModelFactory.getInstance(app))[DiagnosticsExportModel::class.java]
    }

    @Test fun assetAbiMatchesManifestByteOrderAndRejectsIncompleteMetadata() {
        // Values from published alpha.7 and the combined voxel/diagnostics build.
        assertEquals("07329dad", Diagnostics.readEngineAbi(byteArrayOf(0xad.toByte(), 0x9d.toByte(), 0x32, 0x07).inputStream()))
        assertEquals("e85c3cc8", Diagnostics.readEngineAbi(byteArrayOf(0xc8.toByte(), 0x3c, 0x5c, 0xe8.toByte()).inputStream()))
        assertEquals("00000001", Diagnostics.readEngineAbi(byteArrayOf(1, 0, 0, 0).inputStream()))
        for (size in listOf(0, 1, 2, 3, 5)) assertNull(Diagnostics.readEngineAbi(ByteArray(size).inputStream()))
    }

    private fun waitUntil(message: String, test: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < deadline) {
            if (test()) return
            SystemClock.sleep(25)
        }
        fail(message)
    }

    @Test fun recordingStartsOffIsBoundedAndDisablingErasesHistoryAndSamples() {
        Diagnostics.record(Diagnostics.Event.GAME_ERROR, "ignored while disabled")
        val disabled = JSONObject(String(Diagnostics.report(app)))
        assertFalse(disabled.getBoolean("recording_enabled"))
        assertEquals(0, disabled.getJSONArray("recent_app_events").length())
        assertTrue("new JNI diagnostics bridge must be linked", disabled.getJSONObject("native").getBoolean("available"))
        assertFalse(disabled.getJSONObject("native").getJSONObject("presentation").getBoolean("measured"))
        inst.runOnMainSync {
            Diagnostics.setRecording(app, true)
            repeat(140) { Diagnostics.record(Diagnostics.Event.LAYOUT, "checkpoint=$it") }
        }
        val enabled = JSONObject(String(Diagnostics.report(app)))
        assertEquals(Diagnostics.EVENT_LIMIT, enabled.getJSONArray("recent_app_events").length())
        assertEquals("checkpoint=139", enabled.getJSONArray("recent_app_events").getJSONObject(Diagnostics.EVENT_LIMIT - 1).getString("detail"))
        inst.runOnMainSync { Diagnostics.setRecording(app, false) }
        val cleared = JSONObject(String(Diagnostics.report(app)))
        assertEquals(0, cleared.getJSONArray("recent_app_events").length())
        assertFalse(cleared.getJSONObject("native").getJSONObject("presentation").getBoolean("measured"))
        assertTrue(cleared.isNull("last_recorded_active_assignment"))
    }

    @Test fun reportExcludesFileContentsPathsAndInvalidRefreshRates() {
        val secret = File(app.filesDir, "diagnostics-private-canary.txt").also(temporary::add)
        secret.writeText("PRIVATE-CONTENTS-DO-NOT-EXPORT-48291")
        val bytes = Diagnostics.report(app)
        val text = bytes.toString(Charsets.UTF_8)
        assertTrue(bytes.size <= Diagnostics.MAX_REPORT_BYTES)
        assertFalse(text.contains(secret.name))
        assertFalse(text.contains(secret.readText()))
        assertFalse(text.contains(app.filesDir.absolutePath))
        assertFalse(text.contains("content://"))
        val report = JSONObject(text)
        assertEquals(BuildConfig.VERSION_NAME, report.getJSONObject("app").getString("version_name"))
        assertTrue(report.getJSONObject("memory").getBoolean("measured"))
        for (rate in listOf(Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, -1f, 0f))
            assertTrue(JSONObject().put("rate", Diagnostics.refreshRate(rate)).isNull("rate"))
        assertEquals(120.0, Diagnostics.refreshRate(120f) as Double, 0.0)
    }

    @Test fun nativeTimingReportCountsPresentationsNotSurfacesAndMarksMissingData() {
        val identity = arrayOf("test fixture", "vendor", "renderer", "GLES 3", "test ABI")
        val native = NativeBridge.DiagnosticSnapshot(identity,
            longArrayOf(1, 1, 2, 2, 1, 1000, 100, 0, 2, 2000, 200, 1000, 1, 4, 0x300D, 2100), intArrayOf())
        val out = Diagnostics.nativeJson(native)
        val timing = out.getJSONObject("presentation")
        assertTrue(timing.getBoolean("measured"))
        assertEquals(2, timing.getInt("retained_presentations"))
        assertEquals(1, timing.getInt("single_surface_presentations"))
        assertEquals(1, timing.getInt("dual_surface_presentations"))
        assertEquals(2, timing.getJSONObject("work").getInt("samples"))
        assertEquals(1, timing.getJSONObject("interval").getInt("samples"))
        assertEquals("egl_swap", out.getJSONArray("recent_graphics_errors").getJSONObject(0).getString("site"))
        for (samples in listOf(longArrayOf(1, 0, 0, 0, 0), longArrayOf(1, 1, 0, 0, 0), longArrayOf(99))) {
            assertFalse(Diagnostics.nativeJson(native.copy(samples = samples)).getJSONObject("presentation").getBoolean("measured"))
        }
    }

    @Test fun liveRendererIdentityAndTimingsComeFromTheActualHarnessContext() {
        ActivityScenario.launch(GameActivity::class.java).use {
            inst.runOnMainSync { Diagnostics.setRecording(app, true) }
            waitUntil("native renderer did not produce diagnostic samples") {
                val samples = NativeBridge.diagnostics()?.samples
                samples != null && samples.size >= 5 && samples[3] >= 3
            }
            val native = JSONObject(String(Diagnostics.report(app))).getJSONObject("native")
            assertTrue(native.getJSONObject("renderer").getBoolean("identity_available"))
            assertTrue(native.getJSONObject("renderer").getString("version").contains("OpenGL ES"))
            assertTrue(native.getJSONObject("renderer").getString("renderer").isNotBlank())
            assertTrue(native.getJSONObject("presentation").getBoolean("measured"))
            assertTrue(native.getJSONObject("presentation").getJSONObject("work").getDouble("max_ms") >= 0)
        }
    }

    @Test fun canceledPickerOpensNothingAndCanBeRequestedAgain() {
        val model = model()
        var opens = 0
        model.openOutput = { opens++; ByteArrayOutputStream() }
        inst.runOnMainSync {
            assertTrue(model.begin()!!.endsWith(".json"))
            assertNull(model.begin())
            model.destination(null)
            assertEquals(DiagnosticsExportModel.Phase.IDLE, model.state.value!!.phase)
            assertNotNull(model.begin())
            model.pickerFailed()
            assertEquals(DiagnosticsExportModel.Result.PICKER_FAILED, model.state.value!!.result)
            model.consumeResult()
            assertNotNull(model.begin())
            model.destination(null)
        }
        assertEquals(0, opens)
    }

    @Test fun exportWorkerSurvivesSettingsRecreationWithoutDuplicateWrite() {
        val started = CountDownLatch(1)
        val finish = CountDownLatch(1)
        val opens = AtomicInteger()
        val closes = AtomicInteger()
        val output = object : ByteArrayOutputStream() {
            override fun write(bytes: ByteArray, offset: Int, count: Int) {
                check(Looper.myLooper() != Looper.getMainLooper())
                started.countDown()
                check(finish.await(15, TimeUnit.SECONDS))
                super.write(bytes, offset, count)
            }
            override fun close() { closes.incrementAndGet(); super.close() }
        }
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            lateinit var retained: DiagnosticsExportModel
            try {
                scenario.onActivity {
                    val fragment = it.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
                    retained = fragment.diagnosticsModel
                    retained.openOutput = { opens.incrementAndGet(); output }
                    assertNotNull(retained.begin())
                    retained.destination(Uri.parse("content://diagnostics-fixture/private-destination"))
                }
                assertTrue(started.await(5, TimeUnit.SECONDS))
                scenario.recreate()
                scenario.onActivity {
                    val fragment = it.supportFragmentManager.findFragmentById(R.id.settings_container) as SettingsActivity.SettingsFragment
                    assertSame(retained, fragment.diagnosticsModel)
                    assertEquals(DiagnosticsExportModel.Phase.WRITING, retained.state.value!!.phase)
                    assertNull(retained.begin())
                    retained.destination(Uri.parse("content://diagnostics-fixture/duplicate"))
                }
                finish.countDown()
                waitUntil("diagnostics did not complete after recreation") {
                    var idle = false
                    scenario.onActivity { idle = retained.state.value!!.phase == DiagnosticsExportModel.Phase.IDLE }
                    idle
                }
                assertEquals(1, opens.get()); assertEquals(1, closes.get())
                assertEquals(1, JSONObject(output.toString("UTF-8")).getInt("schema_version"))
                assertFalse(output.toString("UTF-8").contains("private-destination"))
            } finally {
                finish.countDown()
            }
        }
    }

    @Test fun openWriteAndCloseFailuresDoNotExposeProviderMessagesOrLeakStreams() {
        inst.runOnMainSync { Diagnostics.setRecording(app, true) }
        for (failure in listOf("open", "write", "close")) {
            val model = model()
            val closed = AtomicInteger()
            model.openOutput = {
                if (failure == "open") throw IOException("PRIVATE-provider-path-49281")
                object : OutputStream() {
                    override fun write(value: Int) { if (failure == "write") throw IOException("PRIVATE-provider-path-49281") }
                    override fun close() {
                        closed.incrementAndGet()
                        if (failure == "close") throw IOException("PRIVATE-provider-path-49281")
                    }
                }
            }
            inst.runOnMainSync { model.begin(); model.destination(Uri.parse("content://fixture/private")) }
            waitUntil("failed document write did not complete") {
                var complete = false
                inst.runOnMainSync { complete = model.state.value!!.phase == DiagnosticsExportModel.Phase.COMPLETE }
                complete
            }
            assertEquals(if (failure == "open") 0 else 1, closed.get())
            inst.runOnMainSync { assertEquals(DiagnosticsExportModel.Result.WRITE_FAILED, model.state.value!!.result) }
            assertFalse(String(Diagnostics.report(app)).contains("PRIVATE-provider-path-49281"))
        }
    }

    private class Documents(private val target: File, private val modes: MutableList<String>) : ContentProvider() {
        override fun onCreate() = true
        override fun getType(uri: Uri) = "application/json"
        override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
            modes.add(mode)
            return ParcelFileDescriptor.open(target, ParcelFileDescriptor.parseMode(mode))
        }
        override fun query(uri: Uri, projection: Array<out String>?, selection: String?, args: Array<out String>?, order: String?): Cursor? = null
        override fun insert(uri: Uri, values: ContentValues?): Uri? = null
        override fun update(uri: Uri, values: ContentValues?, selection: String?, args: Array<out String>?) = 0
        override fun delete(uri: Uri, selection: String?, args: Array<out String>?) = 0
    }

    @Test @SdkSuppress(minSdkVersion = 29)
    fun documentWriterNeverTruncatesExistingDataOrOwnedEmptyAliases() {
        val root = File(app.cacheDir, "diagnostics-documents-${System.nanoTime()}").apply { mkdirs() }.also(temporary::add)
        val owned = File(root, "owned").apply { mkdirs() }
        val external = File(root, "external").apply { mkdirs() }
        val target = File(root, "report.json").apply { createNewFile() }
        val modes = mutableListOf<String>()
        fun wrapped(file: File) = object : ContextWrapper(app) {
            override fun getDataDir() = owned
            override fun getFilesDir() = File(owned, "files").apply { mkdirs() }
            override fun getNoBackupFilesDir() = File(owned, "no_backup").apply { mkdirs() }
            override fun getExternalFilesDir(type: String?) = external
            override fun getExternalFilesDirs(type: String?) = arrayOf(external)
            override fun getContentResolver() = ContentResolver.wrap(Documents(file, modes))
        }
        val uri = Uri.parse("content://diagnostics-documents/report")
        DiagnosticsDocuments.open(wrapped(target), uri).use { it.write("report".toByteArray()) }
        assertEquals("report", target.readText())
        fun rejected(file: File) {
            val before = file.readBytes()
            try { DiagnosticsDocuments.open(wrapped(file), uri).use { it.write(1) }; fail("unsafe destination accepted") }
            catch (_: IOException) { }
            assertArrayEquals(before, file.readBytes())
        }
        rejected(target)
        val save = File(external, "emerald3ds.sav").apply { writeBytes(ByteArray(128 * 1024) { 7 }) }
        rejected(save)
        val emptyOwned = File(external, "settings.ini").apply { createNewFile() }
        rejected(emptyOwned)
        val alias = File(root, "owned-alias")
        Os.symlink(emptyOwned.path, alias.path)
        rejected(alias)
        val hardlink = File(root, "hardlink.json")
        try {
            Os.link(emptyOwned.path, hardlink.path)
            rejected(hardlink)
        } catch (failure: ErrnoException) {
            if (failure.errno != OsConstants.EACCES && failure.errno != OsConstants.EPERM) throw failure
            // SELinux can prohibit constructing this fixture. This is not a
            // writer-rejection result; all preceding alias assertions ran.
            assertFalse(hardlink.exists())
            assertEquals(0L, emptyOwned.length())
            Log.i("DiagnosticsTest", "Platform refused hardlink fixture: errno=${failure.errno}; regular/save/owned/symlink checks completed")
        }
        assertTrue(modes.isNotEmpty())
        assertTrue("an open requested truncation", modes.all { it == "rw" })
    }
}
