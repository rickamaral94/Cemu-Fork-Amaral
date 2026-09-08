package info.cemu.cemu.common.customdrivers

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class CustomDriversTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun acceptsSimpleSharedLibraryName() {
        assertTrue(isSafeDriverLibraryName("vulkan.adreno.so"))
    }

    @Test
    fun rejectsLibraryNameWithPathComponents() {
        assertFalse(isSafeDriverLibraryName("../vulkan.adreno.so"))
        assertFalse(isSafeDriverLibraryName("subdir/vulkan.adreno.so"))
        assertFalse(isSafeDriverLibraryName("subdir\\vulkan.adreno.so"))
    }

    @Test
    fun rejectsNonSharedLibraryNames() {
        assertFalse(isSafeDriverLibraryName(""))
        assertFalse(isSafeDriverLibraryName("vulkan.adreno"))
        assertFalse(isSafeDriverLibraryName("."))
        assertFalse(isSafeDriverLibraryName(".."))
    }

    @Test
    fun acceptsSmallMetadataFile() {
        val metadata = temporaryFolder.newFile("meta.json")
        metadata.writeText("{}")

        assertTrue(isValidDriverMetadataFile(metadata.toPath()))
    }

    @Test
    fun rejectsEmptyMetadataFile() {
        val metadata = temporaryFolder.newFile("meta.json")

        assertFalse(isValidDriverMetadataFile(metadata.toPath()))
    }

    @Test
    fun acceptsAarch64ElfSharedObject() {
        val library = temporaryFolder.newFile("vulkan.adreno.so")
        library.writeBytes(elfHeader(type = 3, machine = 183))

        assertTrue(isAarch64ElfSharedLibrary(library.toPath()))
    }

    @Test
    fun validatesCompleteCompatiblePackage() {
        val packageDir = temporaryFolder.newFolder("driver-package").toPath()
        packageDir.resolve("vulkan.adreno.so").toFile()
            .writeBytes(elfHeader(type = 3, machine = 183))

        assertTrue(isDriverPackageCompatible(packageDir, metadata(), deviceApi = 35))
    }

    @Test
    fun rejectsPackageRequiringNewerAndroidApi() {
        val packageDir = temporaryFolder.newFolder("driver-package-newer-api").toPath()
        packageDir.resolve("vulkan.adreno.so").toFile()
            .writeBytes(elfHeader(type = 3, machine = 183))

        assertFalse(
            isDriverPackageCompatible(
                packageDir,
                metadata(minApi = 36),
                deviceApi = 35,
            )
        )
    }

    @Test
    fun rejectsNonAarch64Elf() {
        val library = temporaryFolder.newFile("vulkan.adreno.so")
        library.writeBytes(elfHeader(type = 3, machine = 62))

        assertFalse(isAarch64ElfSharedLibrary(library.toPath()))
    }

    @Test
    fun rejectsExecutableElf() {
        val library = temporaryFolder.newFile("vulkan.adreno.so")
        library.writeBytes(elfHeader(type = 2, machine = 183))

        assertFalse(isAarch64ElfSharedLibrary(library.toPath()))
    }

    @Test
    fun rejectsArbitraryFile() {
        val library = temporaryFolder.newFile("vulkan.adreno.so")
        library.writeText("not an ELF library")

        assertFalse(isAarch64ElfSharedLibrary(library.toPath()))
    }

    private fun metadata(minApi: Int = 30) = DriverMetadata(
        schemaVersion = SUPPORTED_SCHEMA_VERSION,
        name = "Test driver",
        description = "Test",
        author = "Test",
        packageVersion = "1",
        vendor = "Qualcomm",
        driverVersion = "1",
        minApi = minApi,
        libraryName = "vulkan.adreno.so",
    )

    private fun elfHeader(type: Int, machine: Int): ByteArray = ByteArray(64).apply {
        this[0] = 0x7f
        this[1] = 'E'.code.toByte()
        this[2] = 'L'.code.toByte()
        this[3] = 'F'.code.toByte()
        this[4] = 2
        this[5] = 1
        this[6] = 1
        this[16] = type.toByte()
        this[17] = (type shr 8).toByte()
        this[18] = machine.toByte()
        this[19] = (machine shr 8).toByte()
    }
}
