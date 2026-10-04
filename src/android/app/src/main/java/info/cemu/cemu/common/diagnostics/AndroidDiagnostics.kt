package info.cemu.cemu.common.diagnostics

import android.app.ActivityManager
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.BatteryManager
import android.os.PowerManager
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
import java.security.MessageDigest
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

            val userDataDirectory = File(NativeActiveSettings.getUserDataPath())
            val selectedLog = selectDiagnosticLog(userDataDirectory)
            val preparedLog = prepareDiagnosticLog(
                selectedLog?.file ?: userDataDirectory.resolve(CURRENT_LOG_FILE_NAME),
            )
            val selectedDriverPath = NativeSettings.getCustomDriverPath()
            val selectedDriver = selectedDriverPath?.let { path ->
                parseInstalledDrivers(Build.VERSION.SDK_INT).firstOrNull { it.path == path }
            }
            val displayMode = context.display.mode
            val memoryInfo = ActivityManager.MemoryInfo().also { info ->
                context.getSystemService(ActivityManager::class.java).getMemoryInfo(info)
            }
            val performanceExport = exportPerformanceWindows(preparedLog.content)
            val logText = preparedLog.content.orEmpty()
            val battery = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val batteryStatus = battery?.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
            val powerManager = context.getSystemService(PowerManager::class.java)

            val report = DiagnosticReport(
                generatedAtUtc = Instant.now().toString(),
                app = DiagnosticAppInfo(
                    applicationId = BuildConfig.APPLICATION_ID,
                    versionName = BuildConfig.VERSION_NAME,
                    versionCode = BuildConfig.VERSION_CODE,
                    buildType = BuildConfig.BUILD_TYPE,
                    commit = BuildConfig.VERSION_NAME.substringBefore('-'),
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
                    charging = when (batteryStatus) {
                        BatteryManager.BATTERY_STATUS_CHARGING -> "charging"
                        BatteryManager.BATTERY_STATUS_FULL -> "full"
                        BatteryManager.BATTERY_STATUS_DISCHARGING -> "discharging"
                        BatteryManager.BATTERY_STATUS_NOT_CHARGING -> "not-charging"
                        else -> "unavailable"
                    },
                    thermalStatus = runCatching { powerManager.currentThermalStatus.toString() }
                        .getOrDefault("unavailable"),
                    batteryTemperatureCelsius = battery?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
                        ?.takeUnless { it == Int.MIN_VALUE }?.let { (it / 10.0).toString() }
                        ?: "unavailable",
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
                    vulkanReported = extractVulkanReportedInfo(logText),
                    presentation = extractPresentationInfo(logText),
                    gpuTimestamps = logText.lineSequence()
                        .lastOrNull { it.contains("Vulkan: GPU timestamps") }
                        ?.let { parseKeyValues(it.substringAfter("Vulkan: GPU timestamps")) }
                        ?: mapOf("status" to "unavailable: no timestamp capability in selected log"),
                ),
                settings = DiagnosticSettingsInfo(
                    asyncShaderCompile = NativeSettings.getAsyncShaderCompile(),
                    vsyncMode = NativeSettings.getVsyncMode(),
                    accurateBarriers = NativeSettings.getAccurateBarriers(),
                    upscalingFilter = NativeSettings.getUpscalingFilter(),
                    downscalingFilter = NativeSettings.getDownscalingFilter(),
                    diagnosticMode = if (NativeSettings.isOverlayDebugEnabled()) "detailed" else "normal",
                    settingsSha256 = sha256(userDataDirectory.resolve("settings.xml")),
                    activeGraphicPacksSha256 = sha256Text(
                        logText.lineSequence().filter { it.contains("Activate graphic pack:") }
                            .map { it.substringAfter("Activate graphic pack:").trim() }
                            .sorted().joinToString("\n"),
                    ),
                ),
                log = DiagnosticLogInfo(
                    included = preparedLog.content != null,
                    source = selectedLog?.source,
                    sourceBytes = preparedLog.sourceBytes,
                    includedBytes = preparedLog.includedBytes,
                    truncated = preparedLog.truncated,
                    redacted = true,
                ),
                session = extractSessionInfo(logText),
                performance = performanceExport.summary,
            )

            val diagnosticDirectory = context.internalFolder().resolve(DIAGNOSTIC_DIRECTORY)
            val timestamp = DIAGNOSTIC_FILENAME_FORMATTER.format(Instant.now())
            val destination = resolveWithoutConflict(
                diagnosticDirectory,
                "$DIAGNOSTIC_FILE_PREFIX$timestamp$DIAGNOSTIC_FILE_SUFFIX",
            )
            val reportJson = DIAGNOSTIC_JSON.encodeToString(report)

            val additionalEntries = buildMap {
                performanceExport.jsonLines?.let { put(PERFORMANCE_WINDOW_FILE_NAME, it) }
            }
            writeDiagnosticBundle(destination, reportJson, preparedLog, additionalEntries)
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

internal fun extractVulkanReportedInfo(log: String): Map<String, String> {
    val result = linkedMapOf<String, String>()
    log.lineSequence().forEach { line ->
        when {
            line.contains("Using GPU:") -> result["deviceName"] = line.substringAfter("Using GPU:").trim()
            line.contains("Driver version:") -> result["driverInfo"] = line.substringAfter("Driver version:").trim()
            line.contains("Vulkan instance version:") -> result["instanceApiVersion"] = line.substringAfter("Vulkan instance version:").trim()
            line.contains("Vulkan: Device properties") -> parseKeyValues(line.substringAfter("Vulkan: Device properties")).forEach(result::put)
        }
    }
    if (result.isEmpty()) result["status"] = "unavailable: no Vulkan session in selected log"
    return result
}

internal fun extractPresentationInfo(log: String): Map<String, String> {
    val result = linkedMapOf<String, String>()
    log.lineSequence().forEach { line ->
        if (line.contains("Vulkan: Present mode")) parseKeyValues(line.substringAfter("Vulkan: Present mode")).forEach(result::put)
        if (line.contains("Vulkan: Swapchain created")) parseKeyValues(line.substringAfter("Vulkan: Swapchain created")).forEach(result::put)
    }
    if (result.isEmpty()) result["status"] = "unavailable: no swapchain in selected log"
    return result
}

internal fun extractSessionInfo(log: String): Map<String, String> {
    val result = linkedMapOf(
        "titleId" to "unavailable",
        "titleName" to "unavailable",
        "region" to "unavailable",
        "internalResolution" to "unavailable",
        "outputResolution" to "unavailable",
        "sceneMarker" to "unavailable",
    )
    log.lineSequence().forEach { line ->
        when {
            line.contains("TitleId:") -> result["titleId"] = line.substringAfter("TitleId:").trim().replace("-", "")
            line.contains("Title name:") -> result["titleName"] = line.substringAfter("Title name:").trim()
            line.contains("TitleRegion:") -> result["region"] = line.substringAfter("TitleRegion:").trim()
            line.contains("Vulkan: Swapchain created") -> result["outputResolution"] =
                Regex("extent=(\\d+x\\d+)").find(line)?.groupValues?.get(1) ?: result.getValue("outputResolution")
            line.contains("Cemu diagnostic scene:") -> result["sceneMarker"] = line.substringAfter("Cemu diagnostic scene:").trim()
        }
    }
    return result
}

private fun parseKeyValues(text: String): Map<String, String> =
    Regex("(\\w+)=(\\[[^]]*]|[^ ]+)").findAll(text).associate { it.groupValues[1] to it.groupValues[2] }

private fun sha256(file: File): String = if (file.isFile) {
    runCatching { sha256Bytes(file.readBytes()) }.getOrDefault("unavailable")
} else {
    "unavailable"
}

private fun sha256Text(value: String): String = sha256Bytes(value.toByteArray())

private fun sha256Bytes(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
    .digest(bytes).joinToString("") { "%02x".format(it) }
