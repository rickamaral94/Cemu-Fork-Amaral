package info.cemu.cemu.common.diagnostics

import java.io.File
import java.io.RandomAccessFile
import java.nio.charset.StandardCharsets
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption

const val CURRENT_LOG_FILE_NAME = "log.txt"
const val PREVIOUS_SESSION_LOG_FILE_NAME = "previous-session.log"
const val LAST_GAME_SESSION_LOG_FILE_NAME = "last-game-session.log"
const val LAST_CRASH_SESSION_LOG_FILE_NAME = "last-crash-session.log"

private const val ARCHIVE_TRUNCATION_MARKER = "\n--- archived log truncated ---\n"
private const val ARCHIVE_HEADER_BYTES = 64 * 1024
private const val SESSION_MARKER_SCAN_BYTES = 256 * 1024

data class DiagnosticLogSource(
    val file: File,
    val source: String,
)

fun archivePreviousSessionLogs(
    directory: File,
) {
    val currentLog = directory.resolve(CURRENT_LOG_FILE_NAME)
    val previousSessionLog = directory.resolve(PREVIOUS_SESSION_LOG_FILE_NAME)

    if (previousSessionLog.isNonEmptyFile()) {
        when {
            containsCrashMarker(previousSessionLog) -> moveReplacing(
                previousSessionLog,
                directory.resolve(LAST_CRASH_SESSION_LOG_FILE_NAME),
            )

            containsGameSessionMarker(previousSessionLog) -> moveReplacing(
                previousSessionLog,
                directory.resolve(LAST_GAME_SESSION_LOG_FILE_NAME),
            )
        }
    }

    if (currentLog.isNonEmptyFile()) {
        moveReplacing(currentLog, previousSessionLog)
    }
}

fun compactArchivedSessionLogs(
    directory: File,
    maxBytes: Long = MAX_DIAGNOSTIC_LOG_BYTES,
) {
    val archivedLogs = listOf(
        PREVIOUS_SESSION_LOG_FILE_NAME,
        LAST_GAME_SESSION_LOG_FILE_NAME,
        LAST_CRASH_SESSION_LOG_FILE_NAME,
    )

    for (fileName in archivedLogs) {
        val archivedLog = directory.resolve(fileName)
        if (archivedLog.isFile && archivedLog.length() > maxBytes) {
            writeBoundedArchivedLog(
                source = archivedLog,
                destination = archivedLog,
                maxBytes = maxBytes,
            )
        }
    }
}

fun selectDiagnosticLog(directory: File): DiagnosticLogSource? {
    val currentLog = directory.resolve(CURRENT_LOG_FILE_NAME)
    val previousSessionLog = directory.resolve(PREVIOUS_SESSION_LOG_FILE_NAME)
    val lastGameSessionLog = directory.resolve(LAST_GAME_SESSION_LOG_FILE_NAME)
    val lastCrashSessionLog = directory.resolve(LAST_CRASH_SESSION_LOG_FILE_NAME)

    return when {
        currentLog.isNonEmptyFile() && containsGameSessionMarker(currentLog) ->
            DiagnosticLogSource(currentLog, "current-game-session")

        previousSessionLog.isNonEmptyFile() && containsCrashMarker(previousSessionLog) ->
            DiagnosticLogSource(previousSessionLog, "previous-crash-session")

        previousSessionLog.isNonEmptyFile() && containsGameSessionMarker(previousSessionLog) ->
            DiagnosticLogSource(previousSessionLog, "previous-game-session")

        lastCrashSessionLog.isNonEmptyFile() ->
            DiagnosticLogSource(lastCrashSessionLog, "last-crash-session")

        lastGameSessionLog.isNonEmptyFile() ->
            DiagnosticLogSource(lastGameSessionLog, "last-game-session")

        currentLog.isNonEmptyFile() -> DiagnosticLogSource(currentLog, "current-session")
        previousSessionLog.isNonEmptyFile() ->
            DiagnosticLogSource(previousSessionLog, "previous-session")

        else -> null
    }
}

private fun writeBoundedArchivedLog(source: File, destination: File, maxBytes: Long) {
    require(maxBytes > ARCHIVE_HEADER_BYTES && maxBytes <= Int.MAX_VALUE)

    destination.parentFile?.let { parent ->
        if (!parent.isDirectory && !parent.mkdirs()) {
            throw IllegalStateException("Failed to create session log directory")
        }
    }

    val temporaryFile = destination.resolveSibling("${destination.name}.tmp")
    temporaryFile.outputStream().buffered().use { output ->
        if (source.length() <= maxBytes) {
            source.inputStream().buffered().use { input -> input.copyTo(output) }
        } else {
            val markerBytes = ARCHIVE_TRUNCATION_MARKER.toByteArray(StandardCharsets.UTF_8)
            val headerBytes = minOf(ARCHIVE_HEADER_BYTES.toLong(), maxBytes).toInt()
            val tailBytes = (maxBytes - headerBytes - markerBytes.size).toInt()

            RandomAccessFile(source, "r").use { input ->
                val header = ByteArray(headerBytes)
                input.readFully(header)
                output.write(header)
                output.write(markerBytes)

                val tail = ByteArray(tailBytes)
                input.seek(source.length() - tailBytes)
                input.readFully(tail)
                output.write(tail)
            }
        }
    }

    moveReplacing(temporaryFile, destination)
}

private fun moveReplacing(source: File, destination: File) {
    try {
        Files.move(
            source.toPath(),
            destination.toPath(),
            StandardCopyOption.ATOMIC_MOVE,
            StandardCopyOption.REPLACE_EXISTING,
        )
    } catch (_: AtomicMoveNotSupportedException) {
        Files.move(
            source.toPath(),
            destination.toPath(),
            StandardCopyOption.REPLACE_EXISTING,
        )
    }
}

private fun containsGameSessionMarker(file: File): Boolean =
    readLogMarkerSample(file).let { sample ->
        sample.contains("------- Loaded title -------") || sample.contains("TitleId:")
    }

private fun containsCrashMarker(file: File): Boolean =
    readLogMarkerSample(file).let { sample ->
        sample.contains("Unhandled exception") || sample.contains("Game info")
    }

private fun readLogMarkerSample(file: File): String {
    val bytesToRead = minOf(file.length(), SESSION_MARKER_SCAN_BYTES.toLong()).toInt()
    if (bytesToRead == 0) {
        return ""
    }

    val bytes = ByteArray(bytesToRead)
    RandomAccessFile(file, "r").use { input ->
        if (file.length() <= bytesToRead) {
            input.readFully(bytes)
        } else {
            val headerBytes = bytesToRead / 2
            val tailBytes = bytesToRead - headerBytes
            input.readFully(bytes, 0, headerBytes)
            input.seek(file.length() - tailBytes)
            input.readFully(bytes, headerBytes, tailBytes)
        }
    }
    return String(bytes, StandardCharsets.UTF_8)
}

private fun File.isNonEmptyFile(): Boolean = isFile && length() > 0L
