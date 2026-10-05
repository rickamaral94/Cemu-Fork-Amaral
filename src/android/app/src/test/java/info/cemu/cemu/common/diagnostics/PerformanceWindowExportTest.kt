package info.cemu.cemu.common.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PerformanceWindowExportTest {
    @Test
    fun separatesWaitCausesAndLeavesLegacyBreakdownUnavailable() {
        val legacy = parsePerformanceWindow(windowLine(20.0, 40.0), 0)!!
        assertNull(legacy.submittedFenceWaitMs)
        assertNull(legacy.swapchainFenceWaitMs)
        val current = parsePerformanceWindow(windowLine(20.0, 40.0) +
            " submittedFenceWaitMs=12 swapchainFenceWaitMs=3 previousFrameWaitMs=12.5 deviceIdleWaitMs=0 commandProcessingMs=15 continuousDrawPassMs=4 outsideCommandProcessingMs=17", 0)!!
        assertEquals(12.0, current.submittedFenceWaitMs!!, 0.001)
        assertEquals(3.0, current.swapchainFenceWaitMs!!, 0.001)
        assertEquals(12.5, current.previousFrameWaitMs!!, 0.001)
        assertEquals(17.0, current.outsideCommandProcessingMs!!, 0.001)
    }

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

    @Test
    fun detailAfterEnablingDebugBelongsToItsOwnWindow() {
        val log = listOf(
            windowLine(20.0, 40.0),
            windowLine(25.0, 30.0),
            "Cemu Vulkan detailed window v1: frameMs=[median:40.000,p95:45.000,p99:50.000,max:60.000] over16_7=20 over33_3=20 over50=1 samples=20 overheadMs=0.120",
        ).joinToString("\n")

        val export = exportPerformanceWindows(log)

        assertNull(export.windows[0].frameMsMedian)
        assertEquals(40.0, export.windows[1].frameMsMedian!!, 0.001)
        assertEquals(1, export.windows[1].framesOver50Ms)
        assertEquals(0.12, export.windows[1].instrumentationOverheadMs!!, 0.001)
    }

    @Test
    fun sessionTotalsIncludeEveryExportedWindow() {
        val export = exportPerformanceWindows(listOf(windowLine(20.0, 40.0), windowLine(25.0, 30.0)).joinToString("\n"))

        assertEquals(40, export.summary.totalFrames)
        assertEquals(4, export.summary.totalQueueSubmits)
        assertEquals(2, export.summary.totalPresentCalls)
        assertTrue(export.jsonLines!!.contains("ms/frame (window mean)"))
    }

    private fun windowLine(fps: Double, cpu: Double) =
        "Cemu Vulkan window v1: durationMs=1000 fpsEffective=$fps frames=20 renderCpuMs=$cpu commandIdleMs=8 nonIdleMs=32 fenceWaitMs=3 commandBufferFenceWaitMs=2 asyncWaitMs=0 shaderCreateMs=0 queueSubmitCalls=2 commandBuffers=2 queueSubmitCpuMs=0.05 acquireCalls=1 acquireCpuMs=0.2 presentCalls=1 presentCallCpuMs=0.03 presentWaitCalls=0 presentWaitMs=0 swapchainRecreates=0 gpuTimeMs=unavailable gpuReason=timestamp-instrumentation-not-enabled coveragePct=0 droppedSamples=0"

    @Test
    fun exportsGpuFrameMeansAndWeightedCoverageWithoutMixingWindowTotals() {
        val first = parsePerformanceWindow(windowLine(20.0, 40.0), 0)!!.copy(
            gpuTimeMs = 40.0, gpuSamples = 20, gpuFramesObserved = 20,
            gpuTimePerFrameMs = 2.0, gpuFrameMsMax = 7.0,
            gpuCoveragePct = 100.0, gpuUnavailableReason = null,
        )
        val second = first.copy(sequence = 1, gpuTimeMs = 20.0, gpuSamples = 5,
            gpuFramesObserved = 10, gpuTimePerFrameMs = 4.0, gpuCoveragePct = 50.0,
            droppedSamples = 5, gpuUnavailableReason = "timestamp-partial-coverage")
        val summary = summarizePerformanceWindows(listOf(first, second))
        assertEquals(25, summary.totalGpuSamples)
        assertEquals(30, summary.totalGpuFramesObserved)
        assertEquals(83.333333, summary.gpuCoveragePct, 0.00001)
        assertEquals(2.4, summary.gpuTimeMeanMs!!, 0.00001)
        assertEquals(4.0, summary.gpuTimeMs!!.p95, 0.00001)
        assertEquals(7.0, summary.gpuTimeMs!!.max, 0.00001)
        assertEquals(5, summary.droppedSamples)
    }

    @Test
    fun parsesAvailableGpuTimeAndKeepsCompletionBasis() {
        val line = windowLine(30.0, 30.0)
            .replace("gpuTimeMs=unavailable", "gpuTimeMs=75.5")
            .replace("gpuReason=timestamp-instrumentation-not-enabled", "gpuReason=none")
            .replace("coveragePct=0", "coveragePct=100") +
            " gpuSamples=30 gpuFramesObserved=30 gpuTimePerFrameMs=2.516667 gpuFrameMsP95=4.3 gpuFrameMsMax=5.0 gpuTimingCpuMs=0.3 gpuWindowBasis=completed-results"
        val export = exportPerformanceWindows(line)
        val window = export.windows.single()
        assertEquals(75.5, window.gpuTimeMs!!, 0.00001)
        assertEquals(30, window.gpuSamples)
        assertNull(window.gpuUnavailableReason)
        assertEquals("completed-results", window.gpuWindowBasis)
        assertTrue(export.jsonLines!!.contains("completed samples mean"))
    }

    @Test
    fun sessionMaximumPreservesActualStallsBeyondWindowMedian() {
        val window = parsePerformanceWindow(windowLine(30.0, 30.0), 0)!!.copy(
            frameMsMedian = 33.3, frameMsMax = 1029.987,
        )
        val summary = summarizePerformanceWindows(listOf(window))
        assertEquals(33.3, summary.frameTimeMs!!.median, 0.00001)
        assertEquals(1029.987, summary.frameTimeMs!!.max, 0.00001)
    }
}
