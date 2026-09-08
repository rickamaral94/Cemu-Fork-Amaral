package info.cemu.cemu.common.io

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class ZipUtilsTest {
    @Test
    fun `extracts nested files without explicit directory entries`() {
        val targetDir = Files.createTempDirectory("cemu-zip-test")

        try {
            val content = "driver metadata".toByteArray()
            unzip(zipOf("nested/meta.json" to content).inputStream(), targetDir)

            assertArrayEquals(content, Files.readAllBytes(targetDir.resolve("nested/meta.json")))
        } finally {
            targetDir.toFile().deleteRecursively()
        }
    }

    @Test
    fun `rejects entries that escape the destination directory`() {
        val parentDir = Files.createTempDirectory("cemu-zip-test")
        val targetDir = parentDir.resolve("target")
        val escapedFile = parentDir.resolve("escaped.txt")

        try {
            assertThrows(InvalidZipArchiveException::class.java) {
                unzip(zipOf("../escaped.txt" to byteArrayOf(1)).inputStream(), targetDir)
            }

            assertFalse(Files.exists(escapedFile))
        } finally {
            parentDir.toFile().deleteRecursively()
        }
    }

    @Test
    fun `rejects archives with too many entries`() {
        val targetDir = Files.createTempDirectory("cemu-zip-test")

        try {
            assertThrows(InvalidZipArchiveException::class.java) {
                unzip(
                    stream = zipOf(
                        "one" to byteArrayOf(1),
                        "two" to byteArrayOf(2),
                    ).inputStream(),
                    targetDir = targetDir,
                    limits = ZipExtractionLimits(maxEntries = 1),
                )
            }
        } finally {
            targetDir.toFile().deleteRecursively()
        }
    }

    @Test
    fun `rejects entries over the uncompressed size limit`() {
        val targetDir = Files.createTempDirectory("cemu-zip-test")

        try {
            assertThrows(InvalidZipArchiveException::class.java) {
                unzip(
                    stream = zipOf("large" to ByteArray(9)).inputStream(),
                    targetDir = targetDir,
                    limits = ZipExtractionLimits(maxEntryUncompressedBytes = 8),
                )
            }
        } finally {
            targetDir.toFile().deleteRecursively()
        }
    }

    @Test
    fun `rejects archives over the total uncompressed size limit`() {
        val targetDir = Files.createTempDirectory("cemu-zip-test")

        try {
            assertThrows(InvalidZipArchiveException::class.java) {
                unzip(
                    stream = zipOf(
                        "one" to ByteArray(5),
                        "two" to ByteArray(5),
                    ).inputStream(),
                    targetDir = targetDir,
                    limits = ZipExtractionLimits(maxTotalUncompressedBytes = 8),
                )
            }

            assertFalse(Files.exists(targetDir.resolve("two")))
        } finally {
            targetDir.toFile().deleteRecursively()
        }
    }

    @Test
    fun `does not overwrite duplicate entries`() {
        val targetDir = Files.createTempDirectory("cemu-zip-test")

        try {
            assertThrows(Exception::class.java) {
                unzip(
                    stream = zipOf(
                        "duplicate" to byteArrayOf(1),
                        "duplicate" to byteArrayOf(2),
                    ).inputStream(),
                    targetDir = targetDir,
                )
            }

            assertArrayEquals(byteArrayOf(1), Files.readAllBytes(targetDir.resolve("duplicate")))
        } finally {
            targetDir.toFile().deleteRecursively()
        }
    }

    private fun zipOf(vararg entries: Pair<String, ByteArray>): ByteArray {
        val output = ByteArrayOutputStream()
        ZipOutputStream(output).use { zip ->
            entries.forEach { (name, content) ->
                zip.putNextEntry(ZipEntry(name))
                zip.write(content)
                zip.closeEntry()
            }
        }
        return output.toByteArray()
    }
}
