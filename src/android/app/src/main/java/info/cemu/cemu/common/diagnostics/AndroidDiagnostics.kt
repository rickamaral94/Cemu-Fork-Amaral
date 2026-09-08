package info.cemu.cemu.common.diagnostics

import android.app.ActivityManager
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.os.Build
import android.provider.DocumentsContract
import info.cemu.cemu.BuildConfig
import info.cemu.cemu.common.android.context.internalFolder
import info.cemu.cemu.common.customdrivers.parseInstalledDrivers
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeLogging
import info.cemu.cemu.nativeinterface.NativeSettings
import info.cemu.cemu.provider.DocumentsProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import java.io.File
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

private const val DIAGNOSTIC_DIRECTORY = "diagnostics"
private const val DIAGNOSTIC_FILE_PREFIX = "cemu-fork-amaral-diagnostics-"
private const val DIAGNOSTIC_FILE_SUFFIX = ".zip"
private const val MAX_RETAINED_BUNDLES = 5

suspend fun createAndroidDiagnosticBundle(context: Context): Result<File> =
    withContext(Dispatchers.IO) {
        runCatching {
            NativeLogging.waitForFlush()

            val logFile = File(NativeActiveSettings.getUserDataPath(), "log.txt")
            val preparedLog = prepareDiagnosticLog(logFile)
            val selectedDriverPath = NativeSettings.getCustomDriverPath()
            val selectedDriver = selectedDriverPath?.let { path ->
                parseInstalledDrivers(Build.VERSION.SDK_INT).firstOrNull { it.path == path }
            }
            val displayMode = context.display.mode
            val memoryInfo = ActivityManager.MemoryInfo().also { info ->
                context.getSystemService(ActivityManager::class.java).getMemoryInfo(info)
            }

            val report = DiagnosticReport(
                generatedAtUtc = Instant.now().toString(),
                app = DiagnosticAppInfo(
                    applicationId = BuildConfig.APPLICATION_ID,
                    versionName = BuildConfig.VERSION_NAME,
                    versionCode = BuildConfig.VERSION_CODE,
                    buildType = BuildConfig.BUILD_TYPE,
                ),
                device = DiagnosticDeviceInfo(
                    manufacturer = Build.MANUFACTURER,
                    model = Build.MODEL,
                    androidRelease = Build.VERSION.RELEASE,
                    androidApi = Build.VERSION.SDK_INT,
                    securityPatch = Build.VERSION.SECURITY_PATCH,
                    supportedAbis = Build.SUPPORTED_ABIS.toList(),
                    totalMemoryBytes = memoryInfo.totalMem,
                    displayWidthPixels = displayMode.physicalWidth,
                    displayHeightPixels = displayMode.physicalHeight,
                    displayRefreshRateHz = displayMode.refreshRate,
                ),
                graphics = DiagnosticGraphicsInfo(
                    driverMode = when {
                        selectedDriverPath == null -> "system"
                        selectedDriver == null -> "custom-metadata-unavailable"
                        else -> "custom"
                    },
                    customDriver = selectedDriver?.metadata?.let { metadata ->
                        DiagnosticDriverInfo(
                            name = metadata.name,
                            packageVersion = metadata.packageVersion,
                            vendor = metadata.vendor,
                            driverVersion = metadata.driverVersion,
                            minApi = metadata.minApi,
                        )
                    },
                ),
                settings = DiagnosticSettingsInfo(
                    asyncShaderCompile = NativeSettings.getAsyncShaderCompile(),
                    vsyncMode = NativeSettings.getVsyncMode(),
                    accurateBarriers = NativeSettings.getAccurateBarriers(),
                    upscalingFilter = NativeSettings.getUpscalingFilter(),
                    downscalingFilter = NativeSettings.getDownscalingFilter(),
                ),
                log = DiagnosticLogInfo(
                    included = preparedLog.content != null,
                    sourceBytes = preparedLog.sourceBytes,
                    includedBytes = preparedLog.includedBytes,
                    truncated = preparedLog.truncated,
                    redacted = true,
                ),
            )

            val diagnosticDirectory = context.internalFolder().resolve(DIAGNOSTIC_DIRECTORY)
            val timestamp = DIAGNOSTIC_FILENAME_FORMATTER.format(Instant.now())
            val destination = resolveWithoutConflict(
                diagnosticDirectory,
                "$DIAGNOSTIC_FILE_PREFIX$timestamp$DIAGNOSTIC_FILE_SUFFIX",
            )
            val reportJson = DIAGNOSTIC_JSON.encodeToString(report)

            writeDiagnosticBundle(destination, reportJson, preparedLog)
            deleteOldBundles(diagnosticDirectory, keep = MAX_RETAINED_BUNDLES)
            destination
        }
    }

fun tryShareDiagnosticBundle(context: Context, diagnosticBundle: File): Boolean {
    return try {
        val baseDirectory = context.internalFolder().canonicalFile
        val canonicalBundle = diagnosticBundle.canonicalFile
        if (!canonicalBundle.isFile || !canonicalBundle.toPath().startsWith(baseDirectory.toPath())) {
            return false
        }

        val relativePath = canonicalBundle.relativeTo(baseDirectory).invariantSeparatorsPath
        val fileUri = DocumentsContract.buildDocumentUri(
            DocumentsProvider.AUTHORITY,
            "${DocumentsProvider.ROOT_ID}/$relativePath",
        )
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "application/zip"
            clipData = ClipData.newRawUri(canonicalBundle.name, fileUri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            putExtra(Intent.EXTRA_STREAM, fileUri)
            putExtra(Intent.EXTRA_SUBJECT, canonicalBundle.name)
        }

        context.startActivity(Intent.createChooser(intent, null))
        true
    } catch (_: Exception) {
        false
    }
}

private fun resolveWithoutConflict(directory: File, fileName: String): File {
    if (!directory.isDirectory && !directory.mkdirs()) {
        throw IllegalStateException("Failed to create diagnostics directory")
    }

    val initialFile = directory.resolve(fileName)
    if (!initialFile.exists()) {
        return initialFile
    }

    val baseName = fileName.removeSuffix(DIAGNOSTIC_FILE_SUFFIX)
    var suffix = 1
    while (true) {
        val candidate = directory.resolve("$baseName-$suffix$DIAGNOSTIC_FILE_SUFFIX")
        if (!candidate.exists()) {
            return candidate
        }
        suffix++
    }
}

private fun deleteOldBundles(directory: File, keep: Int) {
    val bundles = directory.listFiles { file ->
        file.isFile && file.name.startsWith(DIAGNOSTIC_FILE_PREFIX) &&
                file.name.endsWith(DIAGNOSTIC_FILE_SUFFIX)
    } ?: return

    bundles.sortedByDescending { it.lastModified() }
        .drop(keep)
        .forEach { it.delete() }
}

private val DIAGNOSTIC_FILENAME_FORMATTER =
    DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'")
        .withZone(ZoneOffset.UTC)

private val DIAGNOSTIC_JSON = Json {
    prettyPrint = true
    encodeDefaults = true
}
