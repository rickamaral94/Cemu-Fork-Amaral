package info.cemu.cemu.common.diagnostics

import info.cemu.cemu.BuildConfig
import info.cemu.cemu.common.customdrivers.DriverMetadata
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeLogging
import info.cemu.cemu.nativeinterface.NativeSettings
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File
import java.security.MessageDigest
import java.time.Instant

internal const val SESSION_CONFIGURATION_PREFIX = "Cemu session configuration v1: "
private val SESSION_JSON = Json { encodeDefaults = true; ignoreUnknownKeys = true }

@Serializable
internal data class SessionConfiguration(
    val schemaVersion: Int = 1,
    val titleId: String,
    val capturedAtUtc: String,
    val app: DiagnosticAppInfo,
    val requestedDriverMode: String,
    val requestedCustomDriver: DiagnosticDriverInfo? = null,
    val settings: DiagnosticSettingsInfo,
)

// Called synchronously after the title profile is applied, immediately before
// the Vulkan loader runs. No current settings are substituted during export.
internal fun logSessionConfiguration(driverPath: String?, titleId: String, profilePath: String) {
    runCatching {
        val directory = File(NativeActiveSettings.getUserDataPath())
        val settingsFile = directory.resolve("settings.xml")
        val driver = driverPath?.takeIf { it.isNotBlank() }?.let { path ->
            runCatching {
                val file = File(path, "meta.json")
                require(file.length() in 1..(64 * 1024))
                SESSION_JSON.decodeFromString<DriverMetadata>(file.readText()).let {
                    DiagnosticDriverInfo(it.name, it.packageVersion, it.vendor, it.driverVersion, it.minApi)
                }
            }.getOrNull()
        }
        val settings = DiagnosticSettingsInfo(
            asyncShaderCompile = NativeSettings.getAsyncShaderCompile(),
            vsyncMode = NativeSettings.getVsyncMode(),
            accurateBarriers = NativeSettings.getAccurateBarriers(),
            upscalingFilter = NativeSettings.getUpscalingFilter(),
            downscalingFilter = NativeSettings.getDownscalingFilter(),
            diagnosticMode = if (NativeSettings.isOverlayDebugEnabled()) "detailed" else "normal",
            settingsSha256 = sha256File(settingsFile),
            activeGraphicPacksSha256 = "unavailable: read from session log at export",
            source = "session-start",
            gameProfileSha256 = sha256Text(normalizeGameProfile(
                File(profilePath).takeIf { it.isFile }?.readText().orEmpty(),
            )),
        )
        val normalizedHash = runCatching {
            val runtime = listOf(settings.asyncShaderCompile, settings.vsyncMode, settings.accurateBarriers,
                settings.upscalingFilter, settings.downscalingFilter, settings.diagnosticMode).joinToString("|")
            sha256Text(normalizeSettingsXml(settingsFile.readText()) + "\n" + runtime)
        }.getOrDefault("unavailable")
        val snapshot = SessionConfiguration(
            titleId = titleId,
            capturedAtUtc = Instant.now().toString(),
            app = DiagnosticAppInfo(BuildConfig.APPLICATION_ID, BuildConfig.VERSION_NAME,
                BuildConfig.VERSION_CODE, BuildConfig.BUILD_TYPE, BuildConfig.VERSION_NAME.substringBefore('-')),
            requestedDriverMode = if (driverPath.isNullOrBlank()) "system" else "custom",
            requestedCustomDriver = driver,
            settings = settings.copy(emulationSettingsSha256 = normalizedHash),
        )
        NativeLogging.log(SESSION_CONFIGURATION_PREFIX + SESSION_JSON.encodeToString(snapshot))
    }.onFailure { NativeLogging.log("Cemu session configuration unavailable: capture-failed") }
}

internal fun extractSessionConfiguration(log: String, titleId: String): SessionConfiguration? {
    // Do not fall back to an older valid marker after a malformed latest marker.
    val line = log.lineSequence().lastOrNull { it.contains(SESSION_CONFIGURATION_PREFIX) } ?: return null
    return runCatching {
        SESSION_JSON.decodeFromString<SessionConfiguration>(line.substringAfter(SESSION_CONFIGURATION_PREFIX))
            .takeIf { it.schemaVersion == 1 && it.titleId == titleId }
    }.getOrNull()
}

internal fun normalizeSettingsXml(xml: String): String {
    // Conservative fingerprint: all settings remain significant except the
    // driver selector. This is not a claim that every field affects emulation.
    val withoutDriver = Regex("<custom_driver_path(?:\\s[^>]*)?>[\\s\\S]*?</custom_driver_path\\s*>|<custom_driver_path(?:\\s[^>]*)?/>")
        .replace(xml, "")
    return Regex(">\\s+<").replace(withoutDriver.trim(), "><")
}

internal fun normalizeGameProfile(profile: String): String {
    var driverSection = false
    return profile.lineSequence().map { it.trim() }.filter { line ->
        if (line.startsWith("[") && line.endsWith("]")) {
            driverSection = line.equals("[AndroidDriver]", ignoreCase = true)
        }
        !driverSection && line.isNotBlank() && !line.startsWith("#") && !line.startsWith(";")
    }.joinToString("\n")
}

internal fun sha256File(file: File): String = runCatching { sha256Bytes(file.readBytes()) }.getOrDefault("unavailable")
internal fun sha256Text(value: String): String = sha256Bytes(value.toByteArray(Charsets.UTF_8))
private fun sha256Bytes(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
    .digest(bytes).joinToString("") { "%02x".format(it) }
