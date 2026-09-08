package info.cemu.cemu.common.diagnostics

import kotlinx.serialization.Serializable
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.charset.StandardCharsets
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

const val DIAGNOSTIC_SCHEMA_VERSION = 2
const val MAX_DIAGNOSTIC_LOG_BYTES = 4L * 1024L * 1024L

@Serializable
data class DiagnosticReport(
    val schemaVersion: Int = DIAGNOSTIC_SCHEMA_VERSION,
    val generatedAtUtc: String,
    val app: DiagnosticAppInfo,
    val device: DiagnosticDeviceInfo,
    val graphics: DiagnosticGraphicsInfo,
    val settings: DiagnosticSettingsInfo,
    val log: DiagnosticLogInfo,
)

@Serializable
data class DiagnosticAppInfo(
    val applicationId: String,
    val versionName: String,
    val versionCode: Int,
    val buildType: String,
)

@Serializable
data class DiagnosticDeviceInfo(
    val manufacturer: String,
    val model: String,
    val androidRelease: String,
    val androidApi: Int,
    val securityPatch: String,
    val supportedAbis: List<String>,
    val totalMemoryBytes: Long,
    val displayWidthPixels: Int?,
    val displayHeightPixels: Int?,
    val displayRefreshRateHz: Float?,
)

@Serializable
data class DiagnosticGraphicsInfo(
    val driverMode: String,
    val customDriver: DiagnosticDriverInfo? = null,
)

@Serializable
data class DiagnosticDriverInfo(
    val name: String,
    val packageVersion: String,
    val vendor: String,
    val driverVersion: String,
    val minApi: Int,
)

@Serializable
data class DiagnosticSettingsInfo(
    val asyncShaderCompile: Boolean,
    val vsyncMode: Int,
    val accurateBarriers: Boolean,
    val upscalingFilter: Int,
    val downscalingFilter: Int,
)

@Serializable
data class DiagnosticLogInfo(
    val included: Boolean,
    val source: String? = null,
    val sourceBytes: Long,
    val includedBytes: Int,
    val truncated: Boolean,
    val redacted: Boolean,
)

data class PreparedDiagnosticLog(
    val content: String?,
    val sourceBytes: Long,
    val includedBytes: Int,
    val truncated: Boolean,
)

fun prepareDiagnosticLog(
    logFile: File,
    maxBytes: Long = MAX_DIAGNOSTIC_LOG_BYTES,
): PreparedDiagnosticLog {
    require(maxBytes > 0 && maxBytes <= Int.MAX_VALUE)

    if (!logFile.isFile) {
        return PreparedDiagnosticLog(
            content = null,
            sourceBytes = 0,
            includedBytes = 0,
            truncated = false,
        )
    }

    val sourceBytes = logFile.length()
    val bytesToRead = minOf(sourceBytes, maxBytes).toInt()
    val rawBytes = ByteArray(bytesToRead)
    var startsAtLineBoundary = true

    RandomAccessFile(logFile, "r").use { input ->
        val startOffset = sourceBytes - bytesToRead
        if (startOffset > 0) {
            input.seek(startOffset - 1)
            startsAtLineBoundary = input.read() == '\n'.code
        } else {
            input.seek(0)
        }
        input.readFully(rawBytes)
    }

    val decodedLog = String(rawBytes, StandardCharsets.UTF_8)
    val rawLog = if (!startsAtLineBoundary) {
        decodedLog.substringAfter('\n', "")
    } else {
        decodedLog
    }
    val sanitizedLog = sanitizeDiagnosticLog(rawLog)
    val sanitizedBytes = sanitizedLog.toByteArray(StandardCharsets.UTF_8)

    return PreparedDiagnosticLog(
        content = sanitizedLog,
        sourceBytes = sourceBytes,
        includedBytes = sanitizedBytes.size,
        truncated = sourceBytes > bytesToRead,
    )
}

fun writeDiagnosticBundle(
    destination: File,
    reportJson: String,
    preparedLog: PreparedDiagnosticLog,
) {
    destination.parentFile?.let { parent ->
        if (!parent.isDirectory && !parent.mkdirs()) {
            throw IOException("Failed to create diagnostics directory")
        }
    }
    if (destination.exists()) {
        throw IOException("Diagnostic bundle already exists")
    }

    val temporaryFile = File(destination.parentFile, "${destination.name}.tmp")
    if (temporaryFile.exists() && !temporaryFile.delete()) {
        throw IOException("Failed to replace incomplete diagnostic bundle")
    }

    try {
        ZipOutputStream(temporaryFile.outputStream().buffered()).use { zip ->
            zip.writeEntry("report.json", reportJson)
            preparedLog.content?.let { zip.writeEntry("log.txt", it) }
        }
        if (!temporaryFile.renameTo(destination)) {
            throw IOException("Failed to finalize diagnostic bundle")
        }
    } catch (exception: Exception) {
        temporaryFile.delete()
        throw exception
    }
}

fun sanitizeDiagnosticLog(log: String): String {
    var sanitized = log
    for ((pattern, replacement) in REDACTION_PATTERNS) {
        sanitized = pattern.replace(sanitized, replacement)
    }
    return sanitized
}

private fun ZipOutputStream.writeEntry(name: String, content: String) {
    putNextEntry(ZipEntry(name).apply { time = 0L })
    write(content.toByteArray(StandardCharsets.UTF_8))
    closeEntry()
}

private val REDACTION_PATTERNS = listOf(
    Regex("(?i)\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,}\\b") to "<email>",
    Regex("(?i)\\b(?:content|file)://[^\\s\\\"'<>]+") to "<uri>",
    Regex("(?i)(?:/storage/emulated/\\d+|/sdcard|/data/user/\\d+|/data/data)/[^\\s\\\"'<>]+") to
            "<android-path>",
    Regex("(?i)\\b(?:[0-9A-F]{2}:){5}[0-9A-F]{2}\\b") to "<mac-address>",
    Regex("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b") to "<ip-address>",
)
