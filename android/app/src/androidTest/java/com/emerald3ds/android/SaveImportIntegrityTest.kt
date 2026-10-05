package com.emerald3ds.android

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
class SaveImportIntegrityTest {
    private lateinit var directory: File
    private lateinit var files: GameFiles

    @Before fun setup() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        directory = File(context.cacheDir, "save-integrity-${System.nanoTime()}").apply { mkdirs() }
        files = GameFiles(object : ContextWrapper(context) {
            override fun getFilesDir() = File(directory, "private").apply { mkdirs() }
            override fun getExternalFilesDir(type: String?) = File(directory, "external").apply { mkdirs() }
        })
        files.ensureDirs()
    }

    @After fun cleanup() { directory.deleteRecursively() }

    private fun input(name: String, bytes: ByteArray): Uri =
        Uri.fromFile(File(directory, name).apply { writeBytes(bytes) })
    private fun backup() = File(files.dataDir, GameFiles.SAVE_NAME + ".bak")
    private fun pending() = File(files.dataDir, GameFiles.SAVE_NAME + ".import")
    private fun reject(bytes: ByteArray) {
        try {
            files.stageImport(input("invalid.sav", bytes), GameFiles.Kind.SAVE)
            fail("corrupt save was staged")
        } catch (_: IOException) { }
    }

    @Test fun twoSameSizedCorruptImportsCannotReplaceCurrentSaveOrItsBackup() {
        val original = EmeraldSaveFixture.create(11, 10)
        val priorBackup = EmeraldSaveFixture.create(10, 8)
        files.saveFile.writeBytes(original)
        backup().writeBytes(priorBackup)
        val corrupt = listOf(ByteArray(128 * 1024), EmeraldSaveFixture.create(12, 12).also {
            it[100] = (it[100].toInt() xor 1).toByte()
        })
        var accepted = 0
        for ((index, bytes) in corrupt.withIndex()) {
            try {
                files.stageImport(input("corrupt-$index.sav", bytes), GameFiles.Kind.SAVE)
                files.applyPendingImports()
                accepted++
            } catch (_: IOException) { }
        }
        val currentIntact = files.saveFile.readBytes().contentEquals(original)
        val backupIntact = backup().readBytes().contentEquals(priorBackup)
        assertTrue("accepted=$accepted, currentIntact=$currentIntact, backupIntact=$backupIntact",
            accepted == 0 && currentIntact && backupIntact)
        assertFalse(files.hasPendingImports())
    }

    @Test fun corruptSectorMetadataPreservesTheAlreadyStagedValidSave() {
        val original = EmeraldSaveFixture.create(21, 20)
        val replacement = EmeraldSaveFixture.create(22, 22, 64 * 1024)
        files.saveFile.writeBytes(original)
        files.stageImport(input("valid.sav", replacement), GameFiles.Kind.SAVE)
        val mutations: List<(ByteArray) -> Unit> = listOf(
            { it[100] = 1 },
            { EmeraldSaveFixture.put16(it, 0xFF6, 0) },
            { EmeraldSaveFixture.put32(it, 0xFF8, 0) },
            { EmeraldSaveFixture.put16(it, 4096 + 0xFF4, 0) },
            { EmeraldSaveFixture.put16(it, 0xFF4, 14) },
            { EmeraldSaveFixture.put32(it, 0xFFC, 1000) },
            { for (sector in 0 until 14) EmeraldSaveFixture.put32(it, sector * 4096 + 0xFFC, 25) }
        )
        for (mutate in mutations) {
            reject(EmeraldSaveFixture.create(23, 24).also(mutate))
            assertArrayEquals(replacement, pending().readBytes())
            assertArrayEquals(original, files.saveFile.readBytes())
        }
        files.applyPendingImports()
        assertArrayEquals(replacement, files.saveFile.readBytes())
        assertArrayEquals(original, backup().readBytes())
    }

    @Test fun stagedSaveIsRevalidatedBeforeReplacingCurrentOrBackup() {
        val original = EmeraldSaveFixture.create(31, 30)
        val priorBackup = EmeraldSaveFixture.create(30, 28)
        files.saveFile.writeBytes(original)
        backup().writeBytes(priorBackup)
        files.stageImport(input("valid.sav", EmeraldSaveFixture.create(32, 32)), GameFiles.Kind.SAVE)
        pending().writeBytes(ByteArray(128 * 1024))
        try {
            files.applyPendingImports()
            fail("corrupt pending save replaced the live save")
        } catch (_: IOException) { }
        assertArrayEquals(original, files.saveFile.readBytes())
        assertArrayEquals(priorBackup, backup().readBytes())
        assertFalse("invalid import caused a permanent startup loop", files.hasPendingImports())
        assertArrayEquals(ByteArray(128 * 1024), File(files.dataDir, pending().name + ".rejected").readBytes())
        files.applyPendingImports() // The user's Retry can load the intact current save.
        assertArrayEquals(original, files.saveFile.readBytes())
        assertArrayEquals(priorBackup, backup().readBytes())
        val replacement = EmeraldSaveFixture.create(33, 32)
        files.stageImport(input("replacement.sav", replacement), GameFiles.Kind.SAVE)
        files.applyPendingImports()
        assertArrayEquals(replacement, files.saveFile.readBytes())
        assertArrayEquals(original, backup().readBytes())
    }

    @Test fun failedQuarantineNeverAppliesCorruptPendingSave() {
        val original = EmeraldSaveFixture.create(35, 34)
        val priorBackup = EmeraldSaveFixture.create(34, 32)
        files.saveFile.writeBytes(original)
        backup().writeBytes(priorBackup)
        pending().writeBytes(ByteArray(128 * 1024))
        val rejected = File(files.dataDir, pending().name + ".rejected").apply { mkdirs() }
        File(rejected, "blocker").writeText("force rename failure")
        try {
            files.applyPendingImports()
            fail("failed quarantine was accepted")
        } catch (_: IOException) { }
        assertArrayEquals(original, files.saveFile.readBytes())
        assertArrayEquals(priorBackup, backup().readBytes())
        assertTrue(files.hasPendingImports())
        rejected.deleteRecursively()
        try { files.applyPendingImports(); fail("invalid pending save was not reported") }
        catch (_: IOException) { }
        files.applyPendingImports()
        assertArrayEquals(original, files.saveFile.readBytes())
        assertArrayEquals(priorBackup, backup().readBytes())
    }

    @Test fun recoverySlotsRotatedSectionsAndCounterWrapKeepExactBytes() {
        val firstSlot64 = EmeraldSaveFixture.create(41, 0x80000000L, 64 * 1024)
        val onlySecond = ByteArray(128 * 1024) { 0xff.toByte() }.also {
            EmeraldSaveFixture.writeSlot(it, 1, 42, 0xFFFFFFFFL, rotation = 7)
        }
        val interrupted = EmeraldSaveFixture.create(43, 48).also {
            EmeraldSaveFixture.writeSlot(it, 1, 44, 49, rotation = 3)
            EmeraldSaveFixture.put32(it, 18 * 4096 + 0xFF8, 0)
        }
        val wrapped = EmeraldSaveFixture.create(45, 0).also {
            EmeraldSaveFixture.writeSlot(it, 1, 46, 0xFFFFFFFFL, rotation = 13)
        }
        for ((index, image) in listOf(firstSlot64, onlySecond, interrupted, wrapped).withIndex()) {
            files.stageImport(input("recover-$index.sav", image), GameFiles.Kind.SAVE)
            files.applyPendingImports()
            assertArrayEquals(image, files.saveFile.readBytes())
        }
    }

    @Test fun incompleteSlotsAreNotCombinedIntoOneSave() {
        val image = EmeraldSaveFixture.create(47, 46).also {
            EmeraldSaveFixture.writeSlot(it, 1, 48, 47)
            EmeraldSaveFixture.put32(it, 0xFF8, 0) // Section 0 missing from slot 0.
            EmeraldSaveFixture.put32(it, 15 * 4096 + 0xFF8, 0) // Section 1 missing from slot 1.
        }
        reject(image)
        assertFalse(files.hasPendingImports())
    }

    @Test fun newerCompleteSlotCannotHideBehindAValidBackup() {
        fun twoSlots() = EmeraldSaveFixture.create(70, 70).also {
            EmeraldSaveFixture.writeSlot(it, 1, 71, 71)
        }
        reject(twoSlots().also {
            EmeraldSaveFixture.put32(it, 14 * 4096 + 0xFFC, 73) // Mixed generations; native still chooses71.
        })
        reject(twoSlots().also {
            for (sector in 14 until 28)
                EmeraldSaveFixture.put32(it, sector * 4096 + 0xFFC, 72) // Wrong physical-slot parity.
        })
        // An older malformed counter is harmless when native selects the
        // newer coherent slot. Do not reject otherwise loadable recovery.
        val recoverable = twoSlots().also { EmeraldSaveFixture.put32(it, 0xFFC, 68) }
        files.stageImport(input("newer-valid.sav", recoverable), GameFiles.Kind.SAVE)
        files.applyPendingImports()
        assertArrayEquals(recoverable, files.saveFile.readBytes())
    }

    @Test fun sectorValidationDoesNotInterpretBadEggOrPartyContents() {
        val image = EmeraldSaveFixture.create(51, 52)
        val world = 4096
        image[world + 0x234] = 1 // party count
        image[world + 0x238 + 19] = 3 // stored Bad Egg and has-species flags
        image[world + 0x238 + 28] = 7 // deliberately do not validate Pokemon checksum
        val checksum = EmeraldSaveFixture.u16(image, world + 0xFF6)
        // The changed words add1 +0x03000000 +7 to the known payload sum.
        EmeraldSaveFixture.put16(image, world + 0xFF6, checksum + 0x308)
        files.stageImport(input("bad-egg.sav", image), GameFiles.Kind.SAVE)
        files.applyPendingImports()
        assertArrayEquals(image, files.saveFile.readBytes())
    }

    @Test fun mgbaNormalizationRequiresAValidFlashSlotAndPreservesOriginalDocument() {
        val raw = EmeraldSaveFixture.create(61, 62)
        val rtc = ByteArray(16).also { it[7] = 0x40 }
        val document = raw + rtc
        val uri = input("clock.sav", document)
        files.stageImport(uri, GameFiles.Kind.SAVE)
        assertArrayEquals(document, File(uri.path!!).readBytes())
        assertArrayEquals(raw, pending().readBytes())
        assertArrayEquals(rtc, File(files.dataDir, GameFiles.MGBA_RTC_ARCHIVE).readBytes())
        reject(ByteArray(128 * 1024) + rtc)
        assertArrayEquals(raw, pending().readBytes())
        files.applyPendingImports()
        assertArrayEquals(raw, files.saveFile.readBytes())
    }
}
