package info.cemu.cemu.common.diagnostics

import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SessionConfigurationTest {
    @get:Rule
    val folder = TemporaryFolder()

    private val titleId = "0005000010143500"
    private fun snapshot(name: String = "driver-a") = SessionConfiguration(
        titleId = titleId,
        capturedAtUtc = "2026-10-05T00:00:00Z",
        app = DiagnosticAppInfo("test.app", "abc-nightly", 1, "release", "abc"),
        requestedDriverMode = "custom",
        requestedCustomDriver = DiagnosticDriverInfo(name, "test", "Mesa", "test", 30),
        settings = DiagnosticSettingsInfo(true, 0, true, 1, 1, "detailed", "raw-a", "packs-a",
            "session-start", "normalized-a", "profile-a"),
    )

    private fun line(value: SessionConfiguration) = SESSION_CONFIGURATION_PREFIX + Json.encodeToString(value)

    @Test
    fun archivedSessionRetainsItsDriverAndSettingsAfterCurrentSessionChanges() {
        val oldLog = folder.root.resolve(CURRENT_LOG_FILE_NAME)
        oldLog.writeText("TitleId: $titleId\n" + line(snapshot()))
        snapshotCompletedGameSessionLog(folder.root)
        oldLog.writeText("menu-only current selection changed to driver-b")
        val selected = selectDiagnosticLog(folder.root)!!
        val captured = extractSessionConfiguration(prepareDiagnosticLog(selected.file).content!!, titleId)!!
        assertEquals("last-completed-game-session", selected.source)
        assertEquals("driver-a", captured.requestedCustomDriver!!.name)
        assertEquals("raw-a", captured.settings.settingsSha256)
        assertEquals("normalized-a", captured.settings.emulationSettingsSha256)
    }

    @Test
    fun missingWrongTitleOrMalformedLatestSnapshotRemainsUnavailable() {
        assertNull(extractSessionConfiguration("legacy log", titleId))
        assertNull(extractSessionConfiguration(line(snapshot()), "different-title"))
        assertNull(extractSessionConfiguration(line(snapshot()) + "\n" + SESSION_CONFIGURATION_PREFIX + "broken", titleId))
        assertEquals("driver-b", extractSessionConfiguration(line(snapshot()) + "\n" + line(snapshot("driver-b")), titleId)!!.requestedCustomDriver!!.name)
    }

    @Test
    fun normalizationIgnoresDriverSelectorButPreservesOtherSettings() {
        val first = "<content><custom_driver_path>/driver/a</custom_driver_path><vsync>0</vsync></content>"
        val second = first.replace("/driver/a", "/driver/b")
        assertEquals(sha256Text(normalizeSettingsXml(first)), sha256Text(normalizeSettingsXml(second)))
        assertNotEquals(sha256Text(normalizeSettingsXml(first)), sha256Text(normalizeSettingsXml(first.replace("<vsync>0", "<vsync>1"))))
        assertEquals(normalizeSettingsXml(first), normalizeSettingsXml(first.replace("><", ">\n  <")))
        assertEquals(normalizeSettingsXml("<content><custom_driver_path/><vsync>0</vsync></content>"), normalizeSettingsXml(first))
    }

    @Test
    fun gameProfileFingerprintExcludesOnlyDriverSection() {
        val profile = "[CPU]\nthreadQuantum=45000\n[AndroidDriver]\nmode=custom\ncustomPath=/driver/a\n[Graphics]\nshaderFastMath=false"
        assertEquals(normalizeGameProfile(profile), normalizeGameProfile(profile.replace("/driver/a", "/driver/b")))
        assertNotEquals(normalizeGameProfile(profile), normalizeGameProfile(profile.replace("45000", "50000")))
        assertNotEquals(normalizeGameProfile(profile), normalizeGameProfile(profile.replace("false", "true")))
    }

    @Test
    fun loaderFallbackAndResolutionPresetAreExplicit() {
        val log = "Vulkan: Android loader mode=system\nActivate graphic pack: Game/Graphics/Resolution [Presets: 1920x1080 (Default)]"
        assertEquals("system", extractAndroidLoaderMode(log))
        assertEquals("1920x1080 (Default)", extractSessionInfo(log)["resolutionGraphicPackPreset"])
        assertEquals("unavailable", extractSessionInfo(log)["internalResolution"])
        assertEquals("unavailable", extractAndroidLoaderMode("legacy log"))
    }
}
