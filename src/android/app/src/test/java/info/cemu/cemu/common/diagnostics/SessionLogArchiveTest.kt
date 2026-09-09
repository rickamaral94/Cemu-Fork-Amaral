package info.cemu.cemu.common.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SessionLogArchiveTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun `archives previous session and identifies game log`() {
        val currentLog = temporaryFolder.newFile(CURRENT_LOG_FILE_NAME)
        currentLog.writeText("------- Loaded title -------\nTitleId: 0005000010143500\nframe")

        archivePreviousSessionLogs(temporaryFolder.root)

        val previousLog = temporaryFolder.root.resolve(PREVIOUS_SESSION_LOG_FILE_NAME)
        assertFalse(currentLog.exists())
        assertTrue(previousLog.readText().contains("TitleId: 0005000010143500"))
        assertEquals(
            "previous-game-session",
            selectDiagnosticLog(temporaryFolder.root)?.source,
        )
    }

    @Test
    fun `library-only session does not replace last game log`() {
        val lastGameLog = temporaryFolder.newFile(LAST_GAME_SESSION_LOG_FILE_NAME)
        lastGameLog.writeText("TitleId: 0005000010143500\nworking game")
        val currentLog = temporaryFolder.newFile(CURRENT_LOG_FILE_NAME)
        currentLog.writeText("Init Cemu\nNo game loaded")

        archivePreviousSessionLogs(temporaryFolder.root)

        assertEquals("TitleId: 0005000010143500\nworking game", lastGameLog.readText())
        assertEquals("last-game-session", selectDiagnosticLog(temporaryFolder.root)?.source)
    }

    @Test
    fun `completed game snapshot is preferred over an older crash`() {
        temporaryFolder.newFile(LAST_CRASH_SESSION_LOG_FILE_NAME)
            .writeText("Unhandled exception from an older build")
        val currentLog = temporaryFolder.newFile(CURRENT_LOG_FILE_NAME)
        currentLog.writeText(
            "------- Loaded title -------\n" +
                "TitleId: 0005000010143500\n" +
                "JIT ARM64 shutdown cleanup: reclaimedFunctions=42 " +
                "reclaimedAllocationBytes=4096",
        )

        snapshotCompletedGameSessionLog(temporaryFolder.root)

        val selectedLog = selectDiagnosticLog(temporaryFolder.root)
        assertEquals("last-completed-game-session", selectedLog?.source)
        assertTrue(selectedLog?.file?.readText()?.contains("reclaimedFunctions=42") == true)
    }

    @Test
    fun `completed game snapshot keeps tail and does not move active log`() {
        val currentLog = temporaryFolder.newFile(CURRENT_LOG_FILE_NAME)
        val header = "------- Loaded title -------\nTitleId: 0005000010143500\n"
        val tail = "JIT ARM64 shutdown cleanup: reclaimedFunctions=7"
        currentLog.writeText(header + "x".repeat(100_000) + tail)

        snapshotCompletedGameSessionLog(temporaryFolder.root, maxBytes = 70_000)

        val snapshot = temporaryFolder.root.resolve(LAST_COMPLETED_GAME_SESSION_LOG_FILE_NAME)
        assertTrue(currentLog.exists())
        assertTrue(snapshot.length() <= 70_000)
        assertTrue(snapshot.readText().startsWith(header))
        assertTrue(snapshot.readText().endsWith(tail))
    }

    @Test
    fun `previous crash is preferred over an older game session`() {
        temporaryFolder.newFile(PREVIOUS_SESSION_LOG_FILE_NAME)
            .writeText("Unhandled exception from java code")
        temporaryFolder.newFile(LAST_GAME_SESSION_LOG_FILE_NAME)
            .writeText("TitleId: 0005000010143500")
        temporaryFolder.newFile(CURRENT_LOG_FILE_NAME).writeText("Init Cemu")

        assertEquals(
            "previous-crash-session",
            selectDiagnosticLog(temporaryFolder.root)?.source,
        )
    }

    @Test
    fun `detects crash marker at end of a large log`() {
        temporaryFolder.newFile(PREVIOUS_SESSION_LOG_FILE_NAME)
            .writeText("x".repeat(300_000) + "\nUnhandled exception from java code")

        assertEquals(
            "previous-crash-session",
            selectDiagnosticLog(temporaryFolder.root)?.source,
        )
    }

    @Test
    fun `promotes game session before replacing previous log`() {
        temporaryFolder.newFile(PREVIOUS_SESSION_LOG_FILE_NAME)
            .writeText("TitleId: 0005000010143500\nworking game")
        temporaryFolder.newFile(CURRENT_LOG_FILE_NAME).writeText("Init Cemu")

        archivePreviousSessionLogs(temporaryFolder.root)

        assertEquals(
            "TitleId: 0005000010143500\nworking game",
            temporaryFolder.root.resolve(LAST_GAME_SESSION_LOG_FILE_NAME).readText(),
        )
        assertEquals(
            "Init Cemu",
            temporaryFolder.root.resolve(PREVIOUS_SESSION_LOG_FILE_NAME).readText(),
        )
        assertEquals("last-game-session", selectDiagnosticLog(temporaryFolder.root)?.source)
    }

    @Test
    fun `large archive keeps header and tail within limit`() {
        val currentLog = temporaryFolder.newFile(CURRENT_LOG_FILE_NAME)
        val header = "TitleId: 0005000010143500\n"
        val tail = "final-vulkan-error"
        currentLog.writeText(header + "x".repeat(100_000) + tail)

        archivePreviousSessionLogs(temporaryFolder.root)
        compactArchivedSessionLogs(temporaryFolder.root, maxBytes = 70_000)

        val archived = temporaryFolder.root.resolve(PREVIOUS_SESSION_LOG_FILE_NAME)
        val archivedText = archived.readText()
        assertTrue(archived.length() <= 70_000)
        assertTrue(archivedText.startsWith(header))
        assertTrue(archivedText.endsWith(tail))
        assertTrue(archivedText.contains("archived log truncated"))
    }

    @Test
    fun `missing current log leaves archive untouched`() {
        val lastGameLog = temporaryFolder.newFile(LAST_GAME_SESSION_LOG_FILE_NAME)
        lastGameLog.writeText("known-good-log")

        archivePreviousSessionLogs(temporaryFolder.root)

        assertEquals("known-good-log", lastGameLog.readText())
        assertFalse(temporaryFolder.root.resolve(PREVIOUS_SESSION_LOG_FILE_NAME).exists())
    }
}
