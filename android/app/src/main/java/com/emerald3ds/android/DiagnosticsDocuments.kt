package com.emerald3ds.android

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.IOException
import java.io.OutputStream

/** Reports only go to new empty documents. No truncation, save copies or content inspection. */
internal object DiagnosticsDocuments {
    fun open(context: Context, uri: Uri): OutputStream {
        val roots = (listOf(context.dataDir, context.filesDir, context.noBackupFilesDir) +
            context.getExternalFilesDirs(null).filterNotNull()).map { it.canonicalFile }.distinct()
        fun inOwnedPath(file: File): Boolean {
            val path = file.canonicalPath
            return roots.any { path == it.path || path.startsWith(it.path + File.separator) }
        }
        if (uri.scheme == "file" && inOwnedPath(File(uri.path ?: throw IOException("missing document path"))))
            throw IOException("diagnostics cannot replace app data")
        // "rw" does not request truncation. Inspect the descriptor before
        // creating a stream; "w"/"wt" would already damage an aliased save.
        val descriptor = context.contentResolver.openFileDescriptor(uri, "rw") ?: throw IOException("document output unavailable")
        try {
            val stat = Os.fstat(descriptor.fileDescriptor)
            if (!OsConstants.S_ISREG(stat.st_mode) || stat.st_size != 0L || stat.st_nlink > 1L)
                throw IOException("choose a new empty regular document")
            val target = Os.readlink("/proc/self/fd/${descriptor.fd}").removeSuffix(" (deleted)")
            if (!target.startsWith("/") || inOwnedPath(File(target))) throw IOException("unsafe document destination")
            // Match directory identities too, covering alternate mount/path
            // names without scanning app files or reading their contents.
            val ownedStats = roots.mapNotNull { runCatching { Os.stat(it.path) }.getOrNull() }
            var parent = File(target).parentFile
            var depth = 0
            while (parent != null) {
                if (++depth > 64) throw IOException("document location unavailable")
                val directory = Os.stat(parent.path)
                if (ownedStats.any { it.st_dev == directory.st_dev && it.st_ino == directory.st_ino })
                    throw IOException("diagnostics cannot replace app data")
                parent = parent.parentFile
            }
            Os.lseek(descriptor.fileDescriptor, 0, OsConstants.SEEK_SET)
            return ParcelFileDescriptor.AutoCloseOutputStream(descriptor)
        } catch (failure: Exception) {
            descriptor.close()
            throw IOException("choose a new empty local document", failure)
        }
    }
}
