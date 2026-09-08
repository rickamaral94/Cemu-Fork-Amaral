package info.cemu.cemu.common.io

import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.InvalidPathException
import java.nio.file.Path
import java.nio.file.Paths
import java.nio.file.StandardOpenOption
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream

data class ZipExtractionLimits(
    val maxEntries: Int = 100_000,
    val maxEntryUncompressedBytes: Long = 256L * 1024 * 1024,
    val maxTotalUncompressedBytes: Long = 1024L * 1024 * 1024,
) {
    init {
        require(maxEntries > 0)
        require(maxEntryUncompressedBytes > 0)
        require(maxTotalUncompressedBytes > 0)
    }
}

class InvalidZipArchiveException(message: String) : IOException(message)

fun unzip(
    stream: InputStream,
    targetDir: String,
    limits: ZipExtractionLimits = ZipExtractionLimits(),
) {
    val normalizedTargetDir = Paths.get(targetDir).toAbsolutePath().normalize()
    Files.createDirectories(normalizedTargetDir)

    ZipInputStream(stream).use { zipInputStream ->
        val buffer = ByteArray(8192)
        var entryCount = 0
        var totalUncompressedBytes = 0L

        var zipEntry: ZipEntry? = zipInputStream.nextEntry

        while (zipEntry != null) {
            entryCount++
            if (entryCount > limits.maxEntries) {
                throw InvalidZipArchiveException(
                    "ZIP contains more than ${limits.maxEntries} entries"
                )
            }

            totalUncompressedBytes = extractZipEntry(
                zipInputStream = zipInputStream,
                zipEntry = zipEntry,
                buffer = buffer,
                targetDir = normalizedTargetDir,
                totalUncompressedBytes = totalUncompressedBytes,
                limits = limits,
            )
            zipInputStream.closeEntry()
            zipEntry = zipInputStream.nextEntry
        }
    }
}

fun unzip(
    stream: InputStream,
    targetDir: Path,
    limits: ZipExtractionLimits = ZipExtractionLimits(),
) = unzip(stream, targetDir.toString(), limits)

private fun extractZipEntry(
    zipInputStream: ZipInputStream,
    zipEntry: ZipEntry,
    buffer: ByteArray,
    targetDir: Path,
    totalUncompressedBytes: Long,
    limits: ZipExtractionLimits,
): Long {
    val entryPath = try {
        Paths.get(zipEntry.name)
    } catch (_: InvalidPathException) {
        throw InvalidZipArchiveException("ZIP contains an invalid path")
    }

    if (zipEntry.name.isBlank() || entryPath.isAbsolute) {
        throw InvalidZipArchiveException("ZIP contains an invalid path")
    }

    val outputPath = targetDir.resolve(entryPath).normalize()
    if (!outputPath.startsWith(targetDir)) {
        throw InvalidZipArchiveException("ZIP entry escapes the destination directory")
    }

    if (zipEntry.isDirectory) {
        Files.createDirectories(outputPath)
        return totalUncompressedBytes
    }

    Files.createDirectories(outputPath.parent)

    val fileOutputStream = Files.newOutputStream(
        outputPath,
        StandardOpenOption.CREATE_NEW,
        StandardOpenOption.WRITE,
    )

    try {
        var entryUncompressedBytes = 0L
        var updatedTotalUncompressedBytes = totalUncompressedBytes
        fileOutputStream.use {
            var bytesRead: Int
            while ((zipInputStream.read(buffer).also { bytesRead = it }) > 0) {
                if (entryUncompressedBytes > limits.maxEntryUncompressedBytes - bytesRead) {
                    throw InvalidZipArchiveException(
                        "ZIP entry exceeds the uncompressed size limit"
                    )
                }
                if (updatedTotalUncompressedBytes > limits.maxTotalUncompressedBytes - bytesRead) {
                    throw InvalidZipArchiveException(
                        "ZIP exceeds the total uncompressed size limit"
                    )
                }

                entryUncompressedBytes += bytesRead
                updatedTotalUncompressedBytes += bytesRead
                it.write(buffer, 0, bytesRead)
            }
        }

        return updatedTotalUncompressedBytes
    } catch (exception: Exception) {
        Files.deleteIfExists(outputPath)
        throw exception
    }
}
