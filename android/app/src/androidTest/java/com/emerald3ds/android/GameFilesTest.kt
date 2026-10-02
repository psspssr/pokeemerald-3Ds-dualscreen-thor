package com.emerald3ds.android

import android.content.Context
import android.content.ContextWrapper
import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.IOException
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GameFilesTest {
    private val app = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var dir: File
    private lateinit var files: GameFiles

    @Before fun setup() {
        dir = File(app.cacheDir, "file-test-${System.nanoTime()}").apply { mkdirs() }
        files = GameFiles(object : ContextWrapper(app) {
            override fun getFilesDir() = File(dir, "files").apply { mkdirs() }
            override fun getExternalFilesDir(type: String?) = File(dir, "external").apply { mkdirs() }
        })
        files.ensureDirs()
    }

    @After fun cleanup() { dir.deleteRecursively() }

    private fun input(name: String, bytes: ByteArray): Uri =
        Uri.fromFile(File(dir, name).apply { writeBytes(bytes) })

    @Test fun importingSaveWaitsForRestartAndKeepsBackup() {
        val old = ByteArray(128 * 1024) { 1 }
        val replacement = ByteArray(128 * 1024) { 2 }
        files.saveFile.writeBytes(old)
        files.stageImport(input("new.sav", replacement), GameFiles.Kind.SAVE)
        assertArrayEquals(old, files.saveFile.readBytes())
        assertTrue(files.hasPendingImports())
        files.applyPendingImports()
        assertArrayEquals(replacement, files.saveFile.readBytes())
        assertArrayEquals(old, File(files.dataDir, "emerald3ds.sav.bak").readBytes())
        assertFalse(files.hasPendingImports())
        files.applyPendingImports()
        assertArrayEquals(old, File(files.dataDir, "emerald3ds.sav.bak").readBytes())
        val out = File(dir, "export.sav")
        files.exportSave(Uri.fromFile(out))
        assertArrayEquals(replacement, out.readBytes())
    }

    @Test fun invalidImportPreservesCurrentAndPreviouslyStagedSave() {
        val old = ByteArray(128 * 1024) { 1 }
        val recovery = ByteArray(64 * 1024) { 2 }
        files.saveFile.writeBytes(old)
        files.stageImport(input("good.sav", recovery), GameFiles.Kind.SAVE)
        for (bad in listOf(ByteArray(0), ByteArray(1), ByteArray(128 * 1024 - 1), ByteArray(128 * 1024 + 1))) {
            try {
                files.stageImport(input("bad.sav", bad), GameFiles.Kind.SAVE)
                fail("invalid save accepted")
            } catch (_: IOException) { }
            assertArrayEquals(old, files.saveFile.readBytes())
            assertTrue(files.hasPendingImports())
            assertFalse(File(files.dataDir, "emerald3ds.sav.tmp").exists())
        }
        files.applyPendingImports()
        assertArrayEquals(recovery, files.saveFile.readBytes())
    }

    @Test fun restoringBackupStagesItAndRejectsOutsideOrTruncatedFiles() {
        val current = ByteArray(128 * 1024) { 7 }
        val previous = ByteArray(128 * 1024) { 3 }
        files.saveFile.writeBytes(current)
        files.backupsDir.mkdirs()
        val backup = File(files.backupsDir, "save-1700000000000-3.sav").apply { writeBytes(previous) }
        File(files.backupsDir, "save-incomplete.tmp").writeBytes(previous)
        assertEquals(listOf(backup), files.saveBackups())
        files.stageBackup(backup)
        assertArrayEquals(current, files.saveFile.readBytes())
        val outside = File(dir, "save-1700000000000-4.sav").apply { writeBytes(previous) }
        val short = File(files.backupsDir, "save-1700000000000-5.sav").apply { writeBytes(ByteArray(30)) }
        for (bad in listOf(outside, short)) {
            try { files.stageBackup(bad); fail("invalid backup accepted") } catch (_: IOException) { }
        }
        files.applyPendingImports()
        assertArrayEquals(previous, files.saveFile.readBytes())
        assertArrayEquals(current, File(files.dataDir, "emerald3ds.sav.bak").readBytes())
    }

    @Test fun backupOrderUsesSnapshotSequenceAcrossClockAndCounterRollback() {
        files.backupsDir.mkdirs()
        val older = File(files.backupsDir, "save-1700000000000-999.sav").apply {
            writeBytes(ByteArray(128 * 1024)); setLastModified(1700000000000)
        }
        val newer = File(files.backupsDir, "save-1700000000001-0.sav").apply {
            writeBytes(ByteArray(128 * 1024)); setLastModified(1600000000000)
        }
        assertEquals(listOf(newer, older), files.saveBackups())
    }

    @Test fun dataPackValidationAndFailedApplyDoNotReportSuccess() {
        try {
            files.stageImport(input("wrong.pak", ByteArray(64)), GameFiles.Kind.PAK)
            fail("invalid pack accepted")
        } catch (_: IOException) { }
        val pack = "EM3DPAK\u0000".toByteArray() + ByteArray(56)
        files.stageImport(input("valid.pak", pack), GameFiles.Kind.PAK)
        files.pakFile.mkdirs()
        File(files.pakFile, "blocker").writeText("blocked")
        try {
            files.applyPendingImports()
            fail("failed rename reported as success")
        } catch (_: IOException) { }
        assertTrue(files.hasPendingImports())
        files.pakFile.deleteRecursively()
        files.applyPendingImports()
        assertArrayEquals(pack, files.pakFile.readBytes())
    }

    @Test fun extractionStampAndEmbeddedDataAreDetected() {
        assertTrue(files.needsExtraction())
        files.extractRomfs { _, _ -> }
        assertFalse(files.needsExtraction())
        assertEquals("fake romfs file\n", File(files.romfsDir, "fake/hello.txt").readText())
        assertFalse(files.hasEmbeddedGameData())
        File(files.romfsDir, "data.embedded").writeText("embedded\n")
        assertTrue(files.hasEmbeddedGameData())
    }

    private fun mgbaFooter() = "26100205123319406f88bf6a00000000".chunked(2).map { it.toInt(16).toByte() }.toByteArray()

    @Test fun mgbaRtcTrailerIsArchivedAndOnlyRawFlashIsImportedAndExported() {
        val raw = ByteArray(128 * 1024) { (it % 251).toByte() }
        val footer = mgbaFooter() // Observed mGBA 0.10.2 RTC layout; synthetic flash data.
        val uri = input("mgba.sav", raw + footer)
        files.stageImport(uri, GameFiles.Kind.SAVE)
        assertArrayEquals(raw + footer, File(uri.path!!).readBytes())
        assertArrayEquals(footer, File(files.dataDir, GameFiles.MGBA_RTC_ARCHIVE).readBytes())
        files.applyPendingImports()
        assertArrayEquals(raw, files.saveFile.readBytes())
        val out = File(dir, "gba-export.sav")
        files.exportSave(Uri.fromFile(out))
        assertEquals(128L * 1024, out.length())
        assertArrayEquals(raw, out.readBytes())
    }

    @Test fun unrelatedOrMalformedTrailersDoNotReplaceAStagedSave() {
        val old = ByteArray(128 * 1024) { 1 }
        val replacement = ByteArray(128 * 1024) { 2 }
        files.saveFile.writeBytes(old)
        files.stageImport(input("good.sav", replacement), GameFiles.Kind.SAVE)
        val malformed = listOf(
            ByteArray(16), // Arbitrary zero padding is not mGBA's initialized RTC.
            mgbaFooter().apply { this[1] = 0x1a }, // Invalid BCD month.
            mgbaFooter().apply { this[1] = 2; this[2] = 0x30 }, // February 30.
            mgbaFooter().apply { this[3] = 7 }, // Invalid weekday.
            mgbaFooter().apply { this[4] = 0x24 }, // Invalid 24-hour time.
            mgbaFooter().apply { this[7] = 1 }, // Unsupported FlashGBX filler/control.
            mgbaFooter().apply { this[15] = 0x7f }, // Implausible Unix timestamp.
        )
        for (footer in malformed) {
            try {
                files.stageImport(input("bad.sav", replacement + footer), GameFiles.Kind.SAVE)
                fail("malformed RTC trailer accepted")
            } catch (_: IOException) { }
            assertArrayEquals(old, files.saveFile.readBytes())
            assertTrue(files.hasPendingImports())
        }
        files.applyPendingImports()
        assertArrayEquals(replacement, files.saveFile.readBytes())
    }

    @Test fun mgbaInitializedRtcBeforeFirstClockReadIsAccepted() {
        val raw = ByteArray(128 * 1024) { 0xff.toByte() }
        val footer = ByteArray(16).apply { this[7] = 0x40 }
        files.stageImport(input("mgba-new.sav", raw + footer), GameFiles.Kind.SAVE)
        files.applyPendingImports()
        assertArrayEquals(raw, files.saveFile.readBytes())
    }
}
