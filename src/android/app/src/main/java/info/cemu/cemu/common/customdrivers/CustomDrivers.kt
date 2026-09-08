package info.cemu.cemu.common.customdrivers

import info.cemu.cemu.common.io.decodeJsonFromFile
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.Path
import kotlin.io.path.isDirectory

const val META_FILE_NAME = "meta.json"
const val SUPPORTED_SCHEMA_VERSION = 1
const val CUSTOM_DRIVERS_DIR_NAME = "customDrivers"
const val MAX_CUSTOM_DRIVER_LIBRARY_BYTES = 256L * 1024L * 1024L
const val MAX_CUSTOM_DRIVER_METADATA_BYTES = 64L * 1024L

private const val ELF_CLASS_64 = 2
private const val ELF_DATA_LITTLE_ENDIAN = 1
private const val ELF_CURRENT_VERSION = 1
private const val ELF_TYPE_SHARED_OBJECT = 3
private const val ELF_MACHINE_AARCH64 = 183

fun getCustomDriversDir(): Path =
    Path(NativeActiveSettings.getUserDataPath()).resolve(CUSTOM_DRIVERS_DIR_NAME)

@Serializable
data class DriverMetadata(
    val schemaVersion: Int,
    val name: String,
    val description: String,
    val author: String,
    val packageVersion: String,
    val vendor: String,
    val driverVersion: String,
    val minApi: Int,
    val libraryName: String,
)

data class Driver(
    val path: String,
    val metadata: DriverMetadata,
)

fun isSafeDriverLibraryName(libraryName: String): Boolean {
    if (libraryName.isBlank() || libraryName.length > 255)
        return false
    if ('/' in libraryName || '\\' in libraryName)
        return false
    if (libraryName == "." || libraryName == ".." || !libraryName.endsWith(".so"))
        return false
    return true
}

fun isValidDriverMetadataFile(metadataPath: Path): Boolean {
    return try {
        Files.isRegularFile(metadataPath) && Files.size(metadataPath) in 1..MAX_CUSTOM_DRIVER_METADATA_BYTES
    } catch (_: Exception) {
        false
    }
}

fun isAarch64ElfSharedLibrary(libraryPath: Path): Boolean {
    return try {
        val size = Files.size(libraryPath)
        if (size < 20 || size > MAX_CUSTOM_DRIVER_LIBRARY_BYTES)
            return false

        val header = ByteArray(20)
        Files.newInputStream(libraryPath).use { input ->
            var offset = 0
            while (offset < header.size) {
                val read = input.read(header, offset, header.size - offset)
                if (read < 0)
                    return false
                offset += read
            }
        }

        header[0] == 0x7f.toByte() &&
                header[1] == 'E'.code.toByte() &&
                header[2] == 'L'.code.toByte() &&
                header[3] == 'F'.code.toByte() &&
                header[4].toInt() == ELF_CLASS_64 &&
                header[5].toInt() == ELF_DATA_LITTLE_ENDIAN &&
                header[6].toInt() == ELF_CURRENT_VERSION &&
                readLittleEndian16(header, 16) == ELF_TYPE_SHARED_OBJECT &&
                readLittleEndian16(header, 18) == ELF_MACHINE_AARCH64
    } catch (_: Exception) {
        false
    }
}

fun isDriverPackageCompatible(
    packagePath: Path,
    metadata: DriverMetadata,
    deviceApi: Int,
): Boolean =
    metadata.schemaVersion == SUPPORTED_SCHEMA_VERSION &&
            metadata.minApi in 1..deviceApi &&
            isSafeDriverLibraryName(metadata.libraryName) &&
            isAarch64ElfSharedLibrary(packagePath.resolve(metadata.libraryName))

private fun readLittleEndian16(bytes: ByteArray, offset: Int): Int =
    (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8)

suspend fun parseInstalledDrivers(deviceApi: Int): List<Driver> {
    return withContext(Dispatchers.IO) {
        val customDriversDir = getCustomDriversDir()

        if (!customDriversDir.isDirectory())
            return@withContext emptyList()

        val driverDirs: Array<File> =
            customDriversDir.toFile().listFiles() ?: return@withContext emptyList()

        val drivers = mutableListOf<Driver>()

        for (driverDir in driverDirs) {
            if (!driverDir.isDirectory)
                continue
            val metadataPath = driverDir.resolve(META_FILE_NAME).toPath()
            if (!isValidDriverMetadataFile(metadataPath))
                continue
            val metadata =
                decodeJsonFromFile<DriverMetadata>(metadataPath.toFile())
                    ?: continue
            if (!isDriverPackageCompatible(driverDir.toPath(), metadata, deviceApi))
                continue
            val driver = Driver(
                path = driverDir.path,
                metadata = metadata,
            )
            drivers.add(driver)
        }

        drivers.sortBy { it.metadata.name }

        return@withContext drivers
    }
}
