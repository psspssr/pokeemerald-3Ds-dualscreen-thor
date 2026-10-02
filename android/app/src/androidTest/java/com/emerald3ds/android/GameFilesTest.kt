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
        files.saveFile.writeBytes(byteArrayOf(1, 2, 3))
        files.stageImport(input("good.sav", byteArrayOf(4, 5, 6)), GameFiles.Kind.SAVE)
        for (bad in listOf(ByteArray(0), ByteArray(128 * 1024 + 1))) {
            try {
                files.stageImport(input("bad.sav", bad), GameFiles.Kind.SAVE)
                fail("invalid save accepted")
            } catch (_: IOException) { }
            assertArrayEquals(byteArrayOf(1, 2, 3), files.saveFile.readBytes())
            assertTrue(files.hasPendingImports())
            assertFalse(File(files.dataDir, "emerald3ds.sav.tmp").exists())
        }
        files.applyPendingImports()
        assertArrayEquals(byteArrayOf(4, 5, 6), files.saveFile.readBytes())
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
}
