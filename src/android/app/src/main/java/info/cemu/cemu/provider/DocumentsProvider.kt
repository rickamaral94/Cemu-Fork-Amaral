// Based on:
// Skyline
// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
package info.cemu.cemu.provider

import android.database.Cursor
import android.database.MatrixCursor
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsProvider
import android.webkit.MimeTypeMap
import info.cemu.cemu.BuildConfig
import info.cemu.cemu.R
import info.cemu.cemu.common.android.context.internalFolder
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.IOException
import java.util.Objects

class DocumentsProvider : DocumentsProvider() {
    private val baseDirectory: File by lazy {
        requireContext().internalFolder().canonicalFile
    }

    private val applicationName: String by lazy {
        var context = requireContext().applicationContext
        context.applicationInfo.loadLabel(context.packageManager).toString()
    }

    override fun onCreate(): Boolean {
        return true
    }

    override fun queryRoots(projection: Array<String>?): Cursor {
        val cursor = MatrixCursor(projection ?: DEFAULT_ROOT_PROJECTION)
        cursor.newRow().add(DocumentsContract.Root.COLUMN_ROOT_ID, ROOT_ID)
            .add(DocumentsContract.Root.COLUMN_SUMMARY, null)
            .add(
                DocumentsContract.Root.COLUMN_FLAGS,
                DocumentsContract.Root.FLAG_SUPPORTS_CREATE or DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD
            )
            .add(DocumentsContract.Root.COLUMN_TITLE, applicationName)
            .add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, getDocumentId(baseDirectory))
            .add(DocumentsContract.Root.COLUMN_MIME_TYPES, "*/*")
            .add(DocumentsContract.Root.COLUMN_AVAILABLE_BYTES, baseDirectory.freeSpace)
            .add(DocumentsContract.Root.COLUMN_ICON, R.mipmap.ic_launcher)
        return cursor
    }

    @Throws(FileNotFoundException::class)
    override fun queryDocument(documentId: String, projection: Array<String>?): Cursor {
        val cursor = MatrixCursor(projection ?: DEFAULT_DOCUMENT_PROJECTION)
        includeFile(cursor, documentId, null)
        return cursor
    }

    override fun isChildDocument(parentDocumentId: String?, documentId: String?): Boolean {
        if (parentDocumentId == null || documentId == null) {
            return false
        }
        return try {
            getFile(documentId).canonicalFile.toPath()
                .startsWith(getFile(parentDocumentId).canonicalFile.toPath())
        } catch (_: FileNotFoundException) {
            false
        }
    }

    @Throws(FileNotFoundException::class)
    override fun createDocument(
        parentDocumentId: String,
        mimeType: String,
        displayName: String,
    ): String {
        val parentFile = getFile(parentDocumentId)
        val newFile = resolveWithoutConflict(parentFile, displayName)

        if (DocumentsContract.Document.MIME_TYPE_DIR == mimeType) {
            if (!newFile.mkdir()) {
                throw FileNotFoundException("Failed to create directory")
            }
        } else {
            try {
                if (!newFile.createNewFile()) {
                    throw FileNotFoundException("Failed to create file")
                }
            } catch (e: IOException) {
                throw RuntimeException(e)
            }
        }

        return getDocumentId(newFile)
    }

    @Throws(FileNotFoundException::class)
    override fun deleteDocument(documentId: String) {
        val file = getFile(documentId)
        if (file == baseDirectory) {
            throw FileNotFoundException("The provider root cannot be deleted")
        }
        if (file.isDirectory) {
            deleteFolder(file)
            return
        }
        if (!file.delete()) {
            throw FileNotFoundException("Couldn't delete document with ID $documentId")
        }
    }

    @Throws(FileNotFoundException::class)
    private fun deleteFolder(dirFile: File) {
        if (!dirFile.isDirectory) {
            return
        }
        val files = dirFile.listFiles() ?: return
        for (file in files) {
            if (file.isDirectory) {
                deleteFolder(file)
                continue
            }
            if (!file.delete()) {
                throw FileNotFoundException("Couldn't delete file ${file.path}")
            }
        }
        if (!dirFile.delete()) {
            throw FileNotFoundException("Couldn't delete file ${dirFile.path}")
        }
    }

    @Throws(FileNotFoundException::class)
    override fun removeDocument(documentId: String, parentDocumentId: String) {
        val parent = getFile(parentDocumentId)
        val file = getFile(documentId)

        if (file == baseDirectory || file.parentFile != parent) {
            throw FileNotFoundException("Couldn't delete document with ID $documentId")
        }
        if (file.isDirectory) {
            deleteFolder(file)
            return
        }
        if (!file.delete()) {
            throw FileNotFoundException("Couldn't delete document with ID $documentId")
        }
    }

    @Throws(FileNotFoundException::class)
    override fun renameDocument(documentId: String, displayName: String?): String {
        if (displayName == null) {
            throw FileNotFoundException("Couldn't rename document $documentId as the new name is null")
        }

        val sourceFile = getFile(documentId)
        if (sourceFile == baseDirectory) {
            throw FileNotFoundException("The provider root cannot be renamed")
        }
        val sourceParentFile = sourceFile.parentFile
            ?: throw FileNotFoundException("Couldn't rename document '$documentId' as it has no parent")
        val destFile = resolve(sourceParentFile, displayName)

        try {
            if (!sourceFile.renameTo(destFile)) {
                throw FileNotFoundException("Couldn't rename document from '${sourceFile.name}' to '${destFile.name}'")
            }
        } catch (exception: Exception) {
            throw FileNotFoundException("Couldn't rename document from '${sourceFile.name}' to 'destFile.name': ${exception.message}")
        }

        return getDocumentId(destFile)
    }

    @Throws(FileNotFoundException::class)
    override fun copyDocument(sourceDocumentId: String, targetParentDocumentId: String): String {
        val parent = getFile(targetParentDocumentId)
        val oldFile = getFile(sourceDocumentId)
        val newFile = resolveWithoutConflict(parent, oldFile.name)

        try {
            if (!(newFile.createNewFile() && newFile.setWritable(true) && newFile.setReadable(true))) {
                throw IOException("Couldn't create new file")
            }
            FileInputStream(oldFile).use { inputStream ->
                FileOutputStream(newFile).use { outputStream ->
                    val b = ByteArray(1024)
                    var len: Int
                    while ((inputStream.read(b, 0, 1024).also { len = it }) > 0) {
                        outputStream.write(b, 0, len)
                    }
                }
            }
        } catch (exception: IOException) {
            throw FileNotFoundException("Couldn't copy document '$sourceDocumentId': ${exception.message}")
        }
        return getDocumentId(newFile)
    }

    @Throws(FileNotFoundException::class)
    override fun moveDocument(
        sourceDocumentId: String,
        sourceParentDocumentId: String,
        targetParentDocumentId: String,
    ): String {
        try {
            val newDocumentId =
                copyDocument(sourceDocumentId, sourceParentDocumentId, targetParentDocumentId)
            removeDocument(sourceDocumentId, sourceParentDocumentId)
            return newDocumentId
        } catch (notFoundException: FileNotFoundException) {
            throw FileNotFoundException("Couldn't move document '$sourceDocumentId' ${notFoundException.message}")
        }
    }

    @Throws(FileNotFoundException::class)
    override fun queryChildDocuments(
        parentDocumentId: String,
        projection: Array<String>?,
        sortOrder: String?,
    ): Cursor {
        val cursor = MatrixCursor(projection ?: DEFAULT_DOCUMENT_PROJECTION)
        val parent = getFile(parentDocumentId)
        val files = parent.listFiles() ?: return cursor
        for (file in files) {
            includeFile(cursor, null, file)
        }
        return cursor
    }

    @Throws(FileNotFoundException::class)
    override fun openDocument(
        documentId: String,
        mode: String,
        signal: CancellationSignal?,
    ): ParcelFileDescriptor {
        val file = getFile(documentId)
        val accessMode = ParcelFileDescriptor.parseMode(mode)
        return ParcelFileDescriptor.open(file, accessMode)
    }

    private fun resolve(file: File, other: String): File {
        val resolvedPath = file.toPath().resolve(other).normalize()
        if (!resolvedPath.startsWith(baseDirectory.toPath())) {
            throw SecurityException("Resolved document is outside the provider root")
        }
        return resolvedPath.toFile()
    }

    @Throws(FileNotFoundException::class)
    private fun copyDocument(
        sourceDocumentId: String,
        sourceParentDocumentId: String,
        targetParentDocumentId: String,
    ): String {
        if (!isChildDocument(sourceParentDocumentId, sourceDocumentId)) {
            throw FileNotFoundException("Couldn't copy document '$sourceDocumentId' as its parent is not '$sourceParentDocumentId'")
        }
        return copyDocument(sourceDocumentId, targetParentDocumentId)
    }

    private fun resolveWithoutConflict(originalFile: File, name: String): File {
        var file = resolve(originalFile, name)
        if (!file.exists()) {
            return file
        }

        // Makes sure two files don't have the same name by adding a number to the end
        var noConflictId = 1
        val periodIndex = name.lastIndexOf('.')
        var extension = ""
        var baseName = name
        if (periodIndex != -1) {
            baseName = name.substring(0, periodIndex)
            extension = name.substring(periodIndex)
        }
        while (file.exists()) {
            val newFileName = "$baseName ($noConflictId)$extension"
            noConflictId++
            file = resolve(originalFile, newFileName)
        }
        return file
    }

    @Throws(FileNotFoundException::class)
    private fun includeFile(cursor: MatrixCursor, documentId: String?, file: File?) {
        val localDocumentId = documentId ?: getDocumentId(file!!)
        val localFile = file ?: getFile(documentId)
        var flags = 0
        if (localFile.isDirectory && localFile.canWrite()) {
            flags = DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE
        } else if (localFile.canWrite()) {
            flags = (DocumentsContract.Document.FLAG_SUPPORTS_WRITE
                    or DocumentsContract.Document.FLAG_SUPPORTS_MOVE
                    or DocumentsContract.Document.FLAG_SUPPORTS_COPY
                    or DocumentsContract.Document.FLAG_SUPPORTS_RENAME)
        }
        if (localFile != baseDirectory) {
            flags = (flags or DocumentsContract.Document.FLAG_SUPPORTS_DELETE
                    or DocumentsContract.Document.FLAG_SUPPORTS_REMOVE)
        }
        cursor.newRow().apply {
            add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, localDocumentId)
            add(
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                if (localFile == baseDirectory) applicationName else localFile.name
            )
            add(DocumentsContract.Document.COLUMN_SIZE, localFile.length())
            add(DocumentsContract.Document.COLUMN_MIME_TYPE, getTypeForFile(localFile))
            add(DocumentsContract.Document.COLUMN_LAST_MODIFIED, localFile.lastModified())
            add(DocumentsContract.Document.COLUMN_FLAGS, flags)
            if (localFile == baseDirectory) {
                add(DocumentsContract.Root.COLUMN_ICON, R.mipmap.ic_launcher)
            }
        }
    }

    private fun getTypeForFile(file: File): String {
        if (file.isDirectory) {
            return DocumentsContract.Document.MIME_TYPE_DIR
        }
        return getTypeForName(file.name)
    }

    private fun getTypeForName(name: String): String {
        val lastDot = name.lastIndexOf('.')
        if (lastDot >= 0) {
            val extension = name.substring(lastDot + 1)
            val mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension)
            if (mime != null) {
                return mime
            }
        }
        return "application/octet-stream"
    }

    @Throws(FileNotFoundException::class)
    private fun getFile(documentId: String?): File {
        Objects.requireNonNull(documentId)
        val relativePath = when {
            documentId == ROOT_ID || documentId == "$ROOT_ID/" -> ""
            documentId!!.startsWith("$ROOT_ID/") -> documentId.substring(ROOT_ID.length + 1)
            else -> {
                throw FileNotFoundException("$documentId is not in any known root")
            }
        }

        val file = try {
            resolve(baseDirectory, relativePath).canonicalFile
        } catch (exception: IOException) {
            throw FileNotFoundException("Invalid document ID $documentId: ${exception.message}")
        } catch (exception: SecurityException) {
            throw FileNotFoundException("$documentId is not in any known root")
        }
        if (!file.toPath().startsWith(baseDirectory.toPath()) || !file.exists()) {
            throw FileNotFoundException("$documentId not found")
        }
        return file
    }

    private fun getDocumentId(file: File): String {
        val canonicalFile = file.canonicalFile
        if (!canonicalFile.toPath().startsWith(baseDirectory.toPath())) {
            throw SecurityException("Document is outside the provider root")
        }
        val relativePath = baseDirectory.toPath()
            .relativize(canonicalFile.toPath())
            .toString()
            .replace(File.separatorChar, '/')
        return if (relativePath.isEmpty()) "$ROOT_ID/" else "$ROOT_ID/$relativePath"
    }

    companion object {
        const val ROOT_ID: String = "root"
        const val AUTHORITY: String = "${BuildConfig.APPLICATION_ID}.provider"
        private val DEFAULT_ROOT_PROJECTION = arrayOf(
            DocumentsContract.Root.COLUMN_ROOT_ID,
            DocumentsContract.Root.COLUMN_MIME_TYPES,
            DocumentsContract.Root.COLUMN_FLAGS,
            DocumentsContract.Root.COLUMN_ICON,
            DocumentsContract.Root.COLUMN_TITLE,
            DocumentsContract.Root.COLUMN_SUMMARY,
            DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_AVAILABLE_BYTES
        )
        private val DEFAULT_DOCUMENT_PROJECTION = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            DocumentsContract.Document.COLUMN_FLAGS,
            DocumentsContract.Document.COLUMN_SIZE
        )
    }
}
