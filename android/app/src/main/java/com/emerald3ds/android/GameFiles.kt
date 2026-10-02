package com.emerald3ds.android

import android.content.Context
import android.net.Uri
import android.os.Build
import android.util.Log
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException

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

    enum class Kind(val fileName: String) { PAK(PAK_NAME), SAVE(SAVE_NAME) }

    fun ensureDirs() {
        dataDir.mkdirs()
    }

    private fun currentStamp(): String {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val code = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else 0L
        return "$code:${info.lastUpdateTime}"
    }

    fun needsExtraction(): Boolean =
        !stampFile.isFile || runCatching { stampFile.readText() }.getOrNull() != currentStamp()

    /** Copies assets/romfs/ to filesDir/romfs/; the stamp is written last. */
    fun extractRomfs(progress: (done: Int, total: Int) -> Unit) {
        stampFile.delete()
        romfsDir.deleteRecursively()
        romfsDir.mkdirs()
        val assets = context.assets
        val files = ArrayList<String>()
        fun walk(path: String) {
            val children = assets.list(path) ?: return
            if (children.isEmpty()) {
                files += path
                return
            }
            for (child in children) walk("$path/$child")
        }
        walk(ASSET_ROOT)
        val buffer = ByteArray(256 * 1024)
        files.forEachIndexed { index, path ->
            val target = File(romfsDir, path.removePrefix("$ASSET_ROOT/"))
            target.parentFile?.mkdirs()
            try {
                assets.open(path).use { input ->
                    target.outputStream().use { output ->
                        while (true) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            output.write(buffer, 0, n)
                        }
                    }
                }
            } catch (e: FileNotFoundException) {
                /* An empty directory lists like a file. */
                target.delete()
                target.mkdirs()
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
                backup.delete()
                target.renameTo(backup)
            }
            if (!pending.renameTo(target)) Log.e(TAG, "could not move $pending to $target")
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
        val input = context.contentResolver.openInputStream(uri) ?: throw IOException("cannot open")
        input.use { stream -> temp.outputStream().use { stream.copyTo(it) } }
        val ok = when (kind) {
            Kind.PAK -> temp.length() >= 64 && temp.inputStream().use { s ->
                val magic = ByteArray(8)
                s.read(magic) == 8 && magic.contentEquals(PAK_MAGIC)
            }
            Kind.SAVE -> temp.length() in 1..SAVE_MAX_BYTES
        }
        if (!ok) {
            temp.delete()
            throw IOException(
                context.getString(if (kind == Kind.PAK) R.string.import_bad_pak else R.string.import_bad_save)
            )
        }
        val pending = pendingFile(kind)
        pending.delete()
        if (!temp.renameTo(pending)) {
            temp.delete()
            throw IOException("rename failed")
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
        /* Port_SaveInit accepts any size up to the 128 KiB flash image. */
        private const val SAVE_MAX_BYTES = 128L * 1024L
        private val PAK_MAGIC = byteArrayOf(0x45, 0x4D, 0x33, 0x44, 0x50, 0x41, 0x4B, 0x00) // "EM3DPAK\0"
    }
}
