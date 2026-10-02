package com.emerald3ds.android

import android.content.Context
import android.net.Uri
import android.os.Build
import android.util.Log
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.time.DateTimeException
import java.time.LocalDate

/**
 * App storage seen by the game: romfs:/ is the RomFS extracted from the APK
 * assets into filesDir/romfs, sdmc:/ is externalFilesDir/sdmc, so the save
 * is sdmc/3ds/emerald3ds/emerald3ds.sav exactly as on a 3DS SD card.
 */
class GameFiles(private val context: Context) {
    val romfsDir = File(context.filesDir, "romfs")
    private val stampFile = File(context.filesDir, "romfs.stamp")
    val sdmcDir: File = File(context.getExternalFilesDir(null) ?: context.filesDir, "sdmc")
    val dataDir = File(sdmcDir, "3ds/emerald3ds")
    val pakFile = File(dataDir, PAK_NAME)
    val saveFile = File(dataDir, SAVE_NAME)
    val backupsDir = File(dataDir, "backups")

    fun saveBackups(): List<File> = backupsDir.listFiles().orEmpty()
        .filter { it.isFile && it.length() == SAVE_MAX_BYTES &&
            it.name.matches(Regex("save-[0-9]+-[0-9]+\\.sav")) && backupTime(it) != null }
        .sortedWith(compareByDescending<File> { backupTime(it) }.thenByDescending { it.name })

    fun backupTime(file: File): Long? = file.name.removePrefix("save-").substringBefore('-').toLongOrNull()

    fun stageBackup(file: File) {
        val candidate = file.canonicalFile
        if (candidate.parentFile != backupsDir.canonicalFile || !candidate.isFile ||
            !candidate.name.matches(Regex("save-[0-9]+-[0-9]+\\.sav")) || candidate.length() != SAVE_MAX_BYTES)
            throw IOException("This backup is no longer available or is incomplete.")
        stageImport(Uri.fromFile(candidate), Kind.SAVE)
    }

    enum class Kind(val fileName: String) { PAK(PAK_NAME), SAVE(SAVE_NAME) }

    fun ensureDirs() {
        if (!dataDir.isDirectory && !dataDir.mkdirs()) throw IOException("could not create data folder")
    }

    fun hasEmbeddedGameData(): Boolean = File(romfsDir, "data.embedded").isFile

    private fun currentStamp(): String {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val code = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else 0L
        return "$code:${info.lastUpdateTime}"
    }

    fun needsExtraction(): Boolean =
        !stampFile.isFile || runCatching { stampFile.readText() }.getOrNull() != currentStamp()

    /** Copies assets/romfs/ to filesDir/romfs/; the stamp is written last. */
    fun extractRomfs(progress: (done: Int, total: Int) -> Unit) {
        if (stampFile.exists() && !stampFile.delete()) throw IOException("could not clear extraction stamp")
        if (romfsDir.exists() && !romfsDir.deleteRecursively()) throw IOException("could not replace game assets")
        if (!romfsDir.mkdirs() && !romfsDir.isDirectory) throw IOException("could not create game assets folder")
        val assets = context.assets
        val files = ArrayList<String>()
        fun walk(path: String) {
            val children = assets.list(path) ?: throw IOException("cannot list asset $path")
            if (children.isEmpty()) {
                if (path == ASSET_ROOT) throw IOException("APK has no game assets")
                files += path
                return
            }
            for (child in children) walk("$path/$child")
        }
        walk(ASSET_ROOT)
        val buffer = ByteArray(256 * 1024)
        files.forEachIndexed { index, path ->
            val target = File(romfsDir, path.removePrefix("$ASSET_ROOT/"))
            val input = try {
                assets.open(path)
            } catch (e: FileNotFoundException) {
                /* An empty directory lists like a file. */
                if (!target.mkdirs() && !target.isDirectory) throw IOException("could not create $target", e)
                null
            }
            input?.use {
                val parent = target.parentFile!!
                if (!parent.isDirectory && !parent.mkdirs()) throw IOException("could not create $parent")
                // A failure opening/writing the output is not an empty asset
                // directory. Propagate it and leave the extraction unstamped.
                target.outputStream().use { output ->
                    while (true) {
                        val n = it.read(buffer)
                        if (n < 0) break
                        output.write(buffer, 0, n)
                    }
                }
            }
            progress(index + 1, files.size)
        }
        stampFile.writeText(currentStamp())
        Log.i(TAG, "extracted ${files.size} RomFS files")
    }

    private fun pendingFile(kind: Kind) = File(dataDir, kind.fileName + PENDING_SUFFIX)

    fun hasPendingImports(): Boolean = Kind.entries.any { pendingFile(it).isFile }

    /**
     * Moves imported files into place. Only called before the game starts in
     * this process, so the game never sees a file change under it.
     */
    fun applyPendingImports() {
        for (kind in Kind.entries) {
            val pending = pendingFile(kind)
            if (!pending.isFile) continue
            val target = File(dataDir, kind.fileName)
            if (kind == Kind.SAVE && target.isFile) {
                val backup = File(dataDir, SAVE_NAME + ".bak")
                val backupTemp = File(dataDir, SAVE_NAME + ".bak.tmp")
                try {
                    target.copyTo(backupTemp, overwrite = true)
                    if (!backupTemp.renameTo(backup)) throw IOException("could not back up existing save")
                } finally {
                    backupTemp.delete()
                }
            }
            if (!pending.renameTo(target)) throw IOException("could not apply imported ${kind.fileName}")
            else Log.i(TAG, "imported ${kind.fileName}")
        }
    }

    /**
     * Copies [uri] next to the target as a pending import after checking it.
     * Throws IOException with a user-facing message on failure.
     */
    fun stageImport(uri: Uri, kind: Kind) {
        ensureDirs()
        val temp = File(dataDir, kind.fileName + ".tmp")
        try {
            val input = context.contentResolver.openInputStream(uri) ?: throw IOException("cannot open")
            input.use { stream -> temp.outputStream().use { out ->
                val buffer = ByteArray(64 * 1024)
                var total = 0L
                while (true) {
                    val n = stream.read(buffer)
                    if (n < 0) break
                    total += n
                    if (kind == Kind.SAVE && total > SAVE_MAX_BYTES + MGBA_RTC_BYTES)
                        throw IOException(context.getString(R.string.import_bad_save))
                    out.write(buffer, 0, n)
                }
            } }
            var rtcFooter: ByteArray? = null
            val ok = when (kind) {
                Kind.PAK -> temp.length() >= 64 && temp.inputStream().use { s ->
                    val magic = ByteArray(8)
                    s.read(magic) == 8 && magic.contentEquals(PAK_MAGIC)
                }
                Kind.SAVE -> {
                    rtcFooter = normalizeSave(temp)
                    true
                }
            }
            if (!ok) throw IOException(
                    context.getString(if (kind == Kind.PAK) R.string.import_bad_pak else R.string.import_bad_save)
                )
            // Same-directory rename replaces a previous pending import atomically.
            if (!temp.renameTo(pendingFile(kind))) throw IOException("could not stage import")
            rtcFooter?.let(::archiveRtcFooter)
        } finally {
            temp.delete()
        }
    }

    /** mGBA 0.10.2's GBASavedataRTCBuffer has no magic or signature. Accept
     * its exact shape with plausible RTC fields, never arbitrary extra data.
     * The app uses the device clock; only the flash image reaches the game.
     */
    private fun normalizeSave(temp: File): ByteArray? {
        if (temp.length() == SAVE_MAX_BYTES || temp.length() == SAVE_MAX_BYTES / 2) return null
        if (temp.length() != SAVE_MAX_BYTES + MGBA_RTC_BYTES)
            throw IOException(context.getString(R.string.import_bad_save))
        return RandomAccessFile(temp, "rw").use { file ->
            val footer = ByteArray(MGBA_RTC_BYTES)
            file.seek(SAVE_MAX_BYTES)
            file.readFully(footer)
            if (!plausibleMgbaRtc(footer)) throw IOException(context.getString(R.string.import_bad_save))
            file.setLength(SAVE_MAX_BYTES)
            footer
        }
    }

    private fun plausibleMgbaRtc(footer: ByteArray): Boolean {
        val control = footer[7].toInt() and 0xff
        val latch = ByteBuffer.wrap(footer, 8, 8).order(ByteOrder.LITTLE_ENDIAN).long
        // MinIRQ, 24-hour mode and power-off are the defined control bits.
        if (control and 0xC8.inv() != 0 || latch !in 0L..253402300799L) return false
        // GBAHardwareInitRTC initializes exactly this state before a clock read.
        if (control == 0x40 && latch == 0L && (0..6).all { footer[it] == 0.toByte() }) return true
        fun bcd(index: Int): Int {
            val v = footer[index].toInt() and 0xff
            return if (v and 15 > 9 || v ushr 4 > 9) -1 else (v ushr 4) * 10 + (v and 15)
        }
        val year = bcd(0)
        if (year !in 0..99 || bcd(3) !in 0..6 || bcd(5) !in 0..59 || bcd(6) !in 0..59) return false
        if (bcd(4) !in 0..(if (control and 0x40 != 0) 23 else 11)) return false
        return try { LocalDate.of(2000 + year, bcd(1), bcd(2)); true }
        catch (_: DateTimeException) { false }
    }

    private fun archiveRtcFooter(footer: ByteArray) {
        val temp = File(dataDir, MGBA_RTC_ARCHIVE + ".tmp")
        try {
            temp.writeBytes(footer)
            if (!temp.renameTo(File(dataDir, MGBA_RTC_ARCHIVE))) throw IOException("could not archive RTC footer")
        } catch (e: IOException) {
            // This optional archive is never read or attached to future exports.
            // The input document remains intact even if archiving fails.
            Log.w(TAG, "save imported, but RTC footer archive was not written", e)
        } finally {
            temp.delete()
        }
    }

    fun exportSave(uri: Uri) {
        if (!saveFile.isFile) throw FileNotFoundException(context.getString(R.string.export_none))
        val output = context.contentResolver.openOutputStream(uri, "wt") ?: throw IOException("cannot open")
        output.use { out -> saveFile.inputStream().use { it.copyTo(out) } }
    }

    companion object {
        private const val TAG = "Emerald"
        private const val ASSET_ROOT = "romfs"
        const val PAK_NAME = "emerald3ds.pak"
        const val SAVE_NAME = "emerald3ds.sav"
        private const val PENDING_SUFFIX = ".import"
        /* Raw Emerald flash saves are 128 KiB; a 64 KiB first-slot image can
         * be recovered by origin, which fills the missing half with 0xFF. */
        private const val SAVE_MAX_BYTES = 128L * 1024L
        private const val MGBA_RTC_BYTES = 16
        internal const val MGBA_RTC_ARCHIVE = "last-imported-mgba-rtc.bin"
        private val PAK_MAGIC = byteArrayOf(0x45, 0x4D, 0x33, 0x44, 0x50, 0x41, 0x4B, 0x00) // "EM3DPAK\0"
    }
}
