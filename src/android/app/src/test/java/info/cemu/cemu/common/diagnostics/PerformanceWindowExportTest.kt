package info.cemu.cemu.common.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PerformanceWindowExportTest {
    @Test
    fun parsesVersionedWindowWithoutInventingGpuTime() {
        val log = """
            Cemu Vulkan window v1: durationMs=1001 fpsEffective=20.00 frames=20 renderCpuMs=40.000 commandIdleMs=8.000 nonIdleMs=32.000 fenceWaitMs=3.000 commandBufferFenceWaitMs=2.000 asyncWaitMs=0.100 shaderCreateMs=0.000 queueSubmitCalls=2 commandBuffers=2 queueSubmitCpuMs=0.050 acquireCalls=1 acquireCpuMs=0.200 presentCalls=1 presentCallCpuMs=0.030 presentWaitCalls=0 presentWaitMs=0.000 swapchainRecreates=0 gpuTimeMs=unavailable gpuReason=timestamp-instrumentation-not-enabled coveragePct=0 droppedSamples=0
        """.trimIndent()

        val export = exportPerformanceWindows(log)

        assertEquals(1, export.windows.size)
        assertEquals(20.0, export.windows.single().fpsEffective, 0.001)
        assertNull(export.windows.single().gpuTimeMs)
        assertEquals("timestamp-instrumentation-not-enabled", export.windows.single().gpuUnavailableReason)
        assertTrue(export.jsonLines!!.contains("\"schemaVersion\":1"))
    }

    @Test
    fun summarizesComparableSessionDistributions() {
        val first = parsePerformanceWindow(windowLine(20.0, 40.0), 0)!!
        val second = parsePerformanceWindow(windowLine(25.0, 30.0), 1)!!

        val summary = summarizePerformanceWindows(listOf(first, second))

        assertEquals(45.0, summary.frameTimeMs!!.median, 5.0)
        assertEquals(40.0, summary.renderCpuMs!!.p95, 0.001)
        assertEquals(40, summary.totalFrames)
    }

    private fun windowLine(fps: Double, cpu: Double) =
        "Cemu Vulkan window v1: durationMs=1000 fpsEffective=$fps frames=20 renderCpuMs=$cpu commandIdleMs=8 nonIdleMs=32 fenceWaitMs=3 commandBufferFenceWaitMs=2 asyncWaitMs=0 shaderCreateMs=0 queueSubmitCalls=2 commandBuffers=2 queueSubmitCpuMs=0.05 acquireCalls=1 acquireCpuMs=0.2 presentCalls=1 presentCallCpuMs=0.03 presentWaitCalls=0 presentWaitMs=0 swapchainRecreates=0 gpuTimeMs=unavailable gpuReason=timestamp-instrumentation-not-enabled coveragePct=0 droppedSamples=0"
}
