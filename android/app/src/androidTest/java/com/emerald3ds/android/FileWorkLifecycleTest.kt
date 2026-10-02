package com.emerald3ds.android

import android.os.Looper
import android.os.SystemClock
import android.app.Application
import android.net.Uri
import androidx.lifecycle.Lifecycle
import androidx.preference.Preference
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.io.File
import java.io.IOException
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** A blocked provider-like file operation must not block or retain the old UI. */
@RunWith(AndroidJUnit4::class)
class FileWorkLifecycleTest {
    @Test fun failedExportRecoveryKeepsPauseAcrossRecreationUntilRetry() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val app = instrumentation.targetContext.applicationContext as Application
        val model = GameFilesModel(app)
        val files = model.files
        files.ensureDirs()
        val oldSave = files.saveFile.takeIf { it.isFile }?.readBytes()
        val recovery = File(app.filesDir, GameFiles.EXPORT_RECOVERY_NAME)
        val original = ByteArray(128 * 1024) { (it % 251).toByte() }
        val output = File(app.cacheDir, "export-recovery-test.sav")
        fun awaitResult() {
            val deadline = SystemClock.uptimeMillis() + 5000
            while (SystemClock.uptimeMillis() < deadline) {
                var done = false
                instrumentation.runOnMainSync { done = model.state.value?.busy == false }
                if (done) return
                SystemClock.sleep(20)
            }
            fail("file worker did not complete")
        }
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            try {
                scenario.onActivity {
                    model.submit(GameFilesModel.Action.EXPORT_SAVE) {
                        check(NativeBridge.awaitPaused())
                        recovery.writeBytes(original)
                        files.saveFile.delete()
                        check(files.saveFile.mkdir()) // Force actual atomic restoration to fail.
                        files.applyPendingImports()
                        throw IOException("restoration unexpectedly succeeded")
                    }
                }
                awaitResult()
                instrumentation.runOnMainSync {
                    assertEquals(app.getString(R.string.export_recovery_pending), model.state.value?.error)
                    assertTrue(GameFilesModel.exportPending)
                }
                scenario.moveToState(Lifecycle.State.CREATED)
                scenario.moveToState(Lifecycle.State.RESUMED)
                scenario.recreate()
                assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                assertArrayEquals(original, recovery.readBytes())
                assertTrue(files.saveFile.delete())
                scenario.onActivity {
                    model.consumeResult()
                    model.exportSave(Uri.fromFile(output))
                }
                awaitResult()
                assertArrayEquals(original, files.saveFile.readBytes())
                assertArrayEquals(original, output.readBytes())
                assertFalse(recovery.exists())
                val deadline = SystemClock.uptimeMillis() + 5000
                while (HostProbe.snapshot()[0] != NativeBridge.STATE_RUNNING && SystemClock.uptimeMillis() < deadline)
                    SystemClock.sleep(20)
                assertEquals(NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
                instrumentation.runOnMainSync { assertFalse(GameFilesModel.exportPending) }
            } finally {
                if (files.saveFile.isDirectory) files.saveFile.delete()
                recovery.delete()
                // Clear a retained hold even if an assertion failed, without
                // leaving the remaining lifecycle tests permanently paused.
                instrumentation.runOnMainSync {
                    model.consumeResult()
                    model.submit(GameFilesModel.Action.EXPORT_SAVE) { false }
                }
                awaitResult()
                output.delete()
                if (oldSave == null) files.saveFile.delete() else files.saveFile.writeBytes(oldSave)
            }
        }
    }

    @Test fun pendingExportKeepsGamePausedAcrossResumeUntilCopyFinishes() {
        val finish = CountDownLatch(1)
        val started = CountDownLatch(1)
        val app = InstrumentationRegistry.getInstrumentation().targetContext.applicationContext as Application
        val model = GameFilesModel(app)
        ActivityScenario.launch(GameActivity::class.java).use { scenario ->
            try {
                scenario.onActivity {
                    model.submit(GameFilesModel.Action.EXPORT_SAVE) {
                        check(NativeBridge.awaitPaused())
                        started.countDown()
                        check(finish.await(10, TimeUnit.SECONDS))
                        false
                    }
                }
                assertTrue(started.await(3, TimeUnit.SECONDS))
                scenario.moveToState(Lifecycle.State.CREATED)
                scenario.moveToState(Lifecycle.State.RESUMED)
                assertEquals(NativeBridge.STATE_PAUSED, HostProbe.snapshot()[0])
                assertTrue(GameFilesModel.exportPending)
                finish.countDown()
                val deadline = SystemClock.uptimeMillis() + 5000
                while (HostProbe.snapshot()[0] != NativeBridge.STATE_RUNNING && SystemClock.uptimeMillis() < deadline)
                    SystemClock.sleep(30)
                assertEquals(NativeBridge.STATE_RUNNING, HostProbe.snapshot()[0])
            } finally {
                finish.countDown()
            }
        }
    }

    @Test fun fileWorkRunsOffMainAndResultSurvivesSettingsRecreation() {
        val started = CountDownLatch(1)
        val finish = CountDownLatch(1)
        var model: GameFilesModel? = null
        ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
            try {
                scenario.onActivity { activity ->
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    model = fragment.fileModel
                    model!!.submit(GameFilesModel.Action.EXPORT_SAVE) {
                        check(Looper.myLooper() != Looper.getMainLooper())
                        started.countDown()
                        check(finish.await(10, TimeUnit.SECONDS))
                        false
                    }
                }
                assertTrue(started.await(3, TimeUnit.SECONDS))
                // This would deadlock if file work ran on the UI thread.
                InstrumentationRegistry.getInstrumentation().runOnMainSync { }
                scenario.recreate()
                scenario.onActivity { activity ->
                    val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                        as SettingsActivity.SettingsFragment
                    assertSame(model, fragment.fileModel)
                    assertTrue(fragment.fileModel.state.value!!.busy)
                    assertFalse(fragment.findPreference<Preference>("import_save")!!.isEnabled)
                }
                finish.countDown()
                val deadline = SystemClock.uptimeMillis() + 5000
                var done = false
                while (!done && SystemClock.uptimeMillis() < deadline) {
                    scenario.onActivity { activity ->
                        val fragment = activity.supportFragmentManager.findFragmentById(R.id.settings_container)
                            as SettingsActivity.SettingsFragment
                        done = fragment.fileModel.state.value == null &&
                            fragment.findPreference<Preference>("import_save")!!.isEnabled
                    }
                    if (!done) SystemClock.sleep(30)
                }
                assertTrue("new UI did not receive completion", done)
            } finally {
                finish.countDown()
            }
        }
    }
}
