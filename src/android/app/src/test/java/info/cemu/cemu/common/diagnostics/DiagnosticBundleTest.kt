package info.cemu.cemu.common.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.util.zip.ZipFile

class DiagnosticBundleTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun redactsPersonalAndDeviceIdentifiersFromLog() {
        val log = """
            user=test@example.com
            game=content://provider/games/windwaker.wua
            path=/storage/emulated/0/Games/WiiU/game.wua
            internal=/data/user/0/io.github.rickamaral94.cemu/files/log.txt
            ip=192.168.1.10 mac=AA:BB:CC:DD:EE:FF
            TitleId: 0005000010143500
        """.trimIndent()

        val sanitized = sanitizeDiagnosticLog(log)

        assertFalse(sanitized.contains("test@example.com"))
        assertFalse(sanitized.contains("content://"))
        assertFalse(sanitized.contains("/storage/emulated/0"))
        assertFalse(sanitized.contains("/data/user/0"))
        assertFalse(sanitized.contains("192.168.1.10"))
        assertFalse(sanitized.contains("AA:BB:CC:DD:EE:FF"))
        assertTrue(sanitized.contains("TitleId: 0005000010143500"))
    }

    @Test
    fun limitsIncludedLogToMostRecentBytes() {
        val logFile = temporaryFolder.newFile("log.txt")
        logFile.writeText("old-data\nnew-data")

        val prepared = prepareDiagnosticLog(logFile, maxBytes = 8)

        assertEquals("new-data", prepared.content)
        assertEquals(17L, prepared.sourceBytes)
        assertTrue(prepared.truncated)
    }

    @Test
    fun writesReportAndPreparedLogToZip() {
        val logFile = temporaryFolder.newFile("log.txt")
        logFile.writeText("TitleId: 0005000010143500")
        val prepared = prepareDiagnosticLog(logFile)
        val bundle = temporaryFolder.root.resolve("diagnostics.zip")

        writeDiagnosticBundle(bundle, "{\"schemaVersion\":1}", prepared)

        ZipFile(bundle).use { zip ->
            val entries = zip.entries().asSequence().map { it.name }.toSet()
            assertEquals(setOf("report.json", "log.txt"), entries)
            assertEquals(
                "{\"schemaVersion\":1}",
                zip.getInputStream(zip.getEntry("report.json")).bufferedReader().readText(),
            )
        }
    }

    @Test
    fun omitsLogEntryWhenNoLogExists() {
        val missingLog = temporaryFolder.root.resolve("missing-log.txt")
        val prepared = prepareDiagnosticLog(missingLog)
        val bundle = temporaryFolder.root.resolve("diagnostics-without-log.zip")

        writeDiagnosticBundle(bundle, "{}", prepared)

        ZipFile(bundle).use { zip ->
            assertTrue(zip.getEntry("report.json") != null)
            assertTrue(zip.getEntry("log.txt") == null)
        }
    }
}
