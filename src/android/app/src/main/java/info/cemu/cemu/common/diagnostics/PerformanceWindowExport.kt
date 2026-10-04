package info.cemu.cemu.common.diagnostics

import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import kotlin.math.ceil

const val PERFORMANCE_WINDOW_SCHEMA_VERSION = 1
const val PERFORMANCE_WINDOW_FILE_NAME = "performance-windows-v1.jsonl"

@Serializable
data class PerformanceWindow(
    val schemaVersion: Int = PERFORMANCE_WINDOW_SCHEMA_VERSION,
    val units: Map<String, String> = PERFORMANCE_WINDOW_UNITS,
    val sequence: Int,
    val durationMs: Double,
    val fpsEffective: Double,
    val frames: Int,
    val drawCallsPerFrame: Int,
    val renderCpuMs: Double,
    val commandIdleMs: Double,
    val nonIdleMs: Double,
    val fenceWaitMs: Double,
    val commandBufferFenceWaitMs: Double,
    val asyncWaitMs: Double,
    val shaderCreateMs: Double,
    val pipelines: Int,
    val pipelineCreations: Int,
    val pipelineChanges: Int,
    val queueSubmitCalls: Int,
    val commandBuffers: Int,
    val queueSubmitCpuMs: Double,
    val acquireCalls: Int,
    val acquireCpuMs: Double,
    val presentCalls: Int,
    val presentCallCpuMs: Double,
    val presentWaitCalls: Int,
    val presentWaitMs: Double,
    val swapchainRecreates: Int,
    val barriers: Int,
    val beginRenderPasses: Int,
    val gpuTimeMs: Double? = null,
    val gpuUnavailableReason: String? = null,
    val gpuCoveragePct: Double,
    val droppedSamples: Int,
    val gpuSamples: Int? = null,
    val gpuFramesObserved: Int? = null,
    val gpuTimePerFrameMs: Double? = null,
    val gpuFrameMsMedian: Double? = null,
    val gpuFrameMsP95: Double? = null,
    val gpuFrameMsP99: Double? = null,
    val gpuFrameMsMax: Double? = null,
    val gpuTimingCpuMs: Double? = null,
    val gpuWindowBasis: String? = null,
    val frameMsMedian: Double? = null,
    val frameMsP95: Double? = null,
    val frameMsP99: Double? = null,
    val frameMsMax: Double? = null,
    val framesOver16_7Ms: Int? = null,
    val framesOver33_3Ms: Int? = null,
    val framesOver50Ms: Int? = null,
    val instrumentationOverheadMs: Double? = null,
)

@Serializable
data class MetricDistribution(
    val median: Double,
    val p95: Double,
    val p99: Double,
    val max: Double,
)

@Serializable
data class PerformanceSessionSummary(
    val windowCount: Int,
    val frameTimeMs: MetricDistribution? = null,
    val renderCpuMs: MetricDistribution? = null,
    val gpuTimeMs: MetricDistribution? = null,
    val gpuCoveragePct: Double,
    val totalFrames: Int,
    val totalQueueSubmits: Int,
    val totalPresentCalls: Int,
    val totalSwapchainRecreates: Int,
    val droppedSamples: Int,
    val totalPipelineCreations: Int = 0,
    val totalGpuSamples: Int = 0,
    val totalGpuFramesObserved: Int = 0,
    val gpuTimeMeanMs: Double? = null,
    val gpuTimingNote: String = "GPU timestamps sum submitted command-buffer intervals per frame, including in-buffer stalls; windows contain completed results. Summary gpuTimeMs is ms/frame (distribution of window means); it is not utilization or display latency.",
    val framesOver16_7Ms: Int? = null,
    val framesOver33_3Ms: Int? = null,
    val framesOver50Ms: Int? = null,
    val note: String = "presentCallCpuMs measures only the host call; it is not visual latency",
)

data class PerformanceWindowExport(
    val windows: List<PerformanceWindow>,
    val jsonLines: String?,
    val summary: PerformanceSessionSummary,
)

fun exportPerformanceWindows(log: String?): PerformanceWindowExport {
    if (log == null) {
        return PerformanceWindowExport(emptyList(), null, emptyPerformanceSummary())
    }
    val windows = mutableListOf<PerformanceWindow>()
    // A detail line belongs to the preceding base window, including when Debug
    // is enabled part-way through a session. Independent list indices shift it.
    log.lineSequence().forEach { line ->
        if (line.contains(WINDOW_PREFIX)) {
            parsePerformanceWindow(line, windows.size)?.let(windows::add)
        } else if (line.contains(DETAILED_PREFIX) && windows.isNotEmpty()) {
            val detail = parseDetailedWindow(line)
            windows[windows.lastIndex] = windows.last().copy(
                frameMsMedian = detail["frameMedian"], frameMsP95 = detail["frameP95"],
                frameMsP99 = detail["frameP99"], frameMsMax = detail["frameMax"],
                framesOver16_7Ms = detail["over16_7"]?.toInt(),
                framesOver33_3Ms = detail["over33_3"]?.toInt(),
                framesOver50Ms = detail["over50"]?.toInt(),
                instrumentationOverheadMs = detail["overheadMs"],
            )
        }
    }
    val jsonLines = windows.takeIf { it.isNotEmpty() }
        ?.joinToString(separator = "\n", postfix = "\n") { WINDOW_JSON.encodeToString(it) }
    return PerformanceWindowExport(windows, jsonLines, summarizePerformanceWindows(windows))
}

internal fun parsePerformanceWindow(line: String, sequence: Int): PerformanceWindow? {
    val payload = line.substringAfter(WINDOW_PREFIX, missingDelimiterValue = "").trim()
    if (payload.isEmpty()) return null
    val values = payload.split(' ').mapNotNull { token ->
        val separator = token.indexOf('=')
        if (separator <= 0) null else token.substring(0, separator) to token.substring(separator + 1)
    }.toMap()
    fun double(name: String) = values[name]?.toDoubleOrNull()
    fun int(name: String) = values[name]?.toIntOrNull()
    return PerformanceWindow(
        sequence = sequence,
        durationMs = double("durationMs") ?: return null,
        fpsEffective = double("fpsEffective") ?: return null,
        frames = int("frames") ?: return null,
        drawCallsPerFrame = int("drawCallsPerFrame") ?: 0,
        renderCpuMs = double("renderCpuMs") ?: return null,
        commandIdleMs = double("commandIdleMs") ?: return null,
        nonIdleMs = double("nonIdleMs") ?: return null,
        fenceWaitMs = double("fenceWaitMs") ?: return null,
        commandBufferFenceWaitMs = double("commandBufferFenceWaitMs") ?: 0.0,
        asyncWaitMs = double("asyncWaitMs") ?: return null,
        shaderCreateMs = double("shaderCreateMs") ?: return null,
        pipelines = int("pipelines") ?: 0,
        pipelineCreations = int("pipelineCreations") ?: 0,
        pipelineChanges = int("pipelineChanges") ?: 0,
        queueSubmitCalls = int("queueSubmitCalls") ?: return null,
        commandBuffers = int("commandBuffers") ?: return null,
        queueSubmitCpuMs = double("queueSubmitCpuMs") ?: return null,
        acquireCalls = int("acquireCalls") ?: return null,
        acquireCpuMs = double("acquireCpuMs") ?: return null,
        presentCalls = int("presentCalls") ?: return null,
        presentCallCpuMs = double("presentCallCpuMs") ?: return null,
        presentWaitCalls = int("presentWaitCalls") ?: return null,
        presentWaitMs = double("presentWaitMs") ?: return null,
        swapchainRecreates = int("swapchainRecreates") ?: return null,
        barriers = int("barriers") ?: 0,
        beginRenderPasses = int("beginRenderPasses") ?: 0,
        gpuTimeMs = double("gpuTimeMs"),
        gpuUnavailableReason = values["gpuReason"]?.takeUnless { it == "none" },
        gpuCoveragePct = double("coveragePct") ?: 0.0,
        droppedSamples = int("droppedSamples") ?: 0,
        gpuSamples = int("gpuSamples"),
        gpuFramesObserved = int("gpuFramesObserved"),
        gpuTimePerFrameMs = double("gpuTimePerFrameMs"),
        gpuFrameMsMedian = double("gpuFrameMsMedian"),
        gpuFrameMsP95 = double("gpuFrameMsP95"),
        gpuFrameMsP99 = double("gpuFrameMsP99"),
        gpuFrameMsMax = double("gpuFrameMsMax"),
        gpuTimingCpuMs = double("gpuTimingCpuMs"),
        gpuWindowBasis = values["gpuWindowBasis"],
    )
}

fun summarizePerformanceWindows(windows: List<PerformanceWindow>): PerformanceSessionSummary {
    if (windows.isEmpty()) return emptyPerformanceSummary()
    val frameTimes = windows.mapNotNull { window -> window.frameMsMedian ?: window.fpsEffective.takeIf { it > 0.0 }?.let { 1000.0 / it } }
    val gpuTimes = windows.mapNotNull { it.gpuTimePerFrameMs }
    val gpuSamples = windows.sumOf { it.gpuSamples ?: 0 }
    val gpuFramesObserved = windows.sumOf { it.gpuFramesObserved ?: 0 }
    val frameDistribution = distribution(frameTimes)?.let { stats ->
        stats.copy(max = windows.mapNotNull { it.frameMsMax }.maxOrNull() ?: stats.max)
    }
    return PerformanceSessionSummary(
        windowCount = windows.size,
        frameTimeMs = frameDistribution,
        renderCpuMs = distribution(windows.map { it.renderCpuMs }),
        gpuTimeMs = distribution(gpuTimes)?.let { stats ->
            stats.copy(max = windows.mapNotNull { it.gpuFrameMsMax }.maxOrNull() ?: stats.max)
        },
        gpuCoveragePct = if (gpuFramesObserved > 0) gpuSamples * 100.0 / gpuFramesObserved
            else windows.map { it.gpuCoveragePct }.average(),
        totalFrames = windows.sumOf { it.frames },
        totalQueueSubmits = windows.sumOf { it.queueSubmitCalls },
        totalPresentCalls = windows.sumOf { it.presentCalls },
        totalSwapchainRecreates = windows.sumOf { it.swapchainRecreates },
        droppedSamples = windows.sumOf { it.droppedSamples },
        totalPipelineCreations = windows.sumOf { it.pipelineCreations },
        totalGpuSamples = gpuSamples,
        totalGpuFramesObserved = gpuFramesObserved,
        gpuTimeMeanMs = if (gpuSamples > 0) windows.sumOf { it.gpuTimeMs ?: 0.0 } / gpuSamples else null,
        framesOver16_7Ms = windows.mapNotNull { it.framesOver16_7Ms }.takeIf { it.isNotEmpty() }?.sum(),
        framesOver33_3Ms = windows.mapNotNull { it.framesOver33_3Ms }.takeIf { it.isNotEmpty() }?.sum(),
        framesOver50Ms = windows.mapNotNull { it.framesOver50Ms }.takeIf { it.isNotEmpty() }?.sum(),
    )
}

private fun distribution(values: List<Double>): MetricDistribution? {
    if (values.isEmpty()) return null
    val sorted = values.sorted()
    fun percentile(value: Double): Double = sorted[(ceil(value * sorted.size).toInt() - 1).coerceIn(sorted.indices)]
    return MetricDistribution(percentile(0.50), percentile(0.95), percentile(0.99), sorted.last())
}

private fun emptyPerformanceSummary() = PerformanceSessionSummary(
    windowCount = 0,
    gpuCoveragePct = 0.0,
    totalFrames = 0,
    totalQueueSubmits = 0,
    totalPresentCalls = 0,
    totalSwapchainRecreates = 0,
    droppedSamples = 0,
)

private const val WINDOW_PREFIX = "Cemu Vulkan window v1:"
private const val DETAILED_PREFIX = "Cemu Vulkan detailed window v1:"

private fun parseDetailedWindow(line: String): Map<String, Double> {
    val result = mutableMapOf<String, Double>()
    Regex("frameMs=\\[median:([0-9.]+),p95:([0-9.]+),p99:([0-9.]+),max:([0-9.]+)]")
        .find(line)?.groupValues?.drop(1)?.map { it.toDouble() }?.let {
            result["frameMedian"] = it[0]; result["frameP95"] = it[1]
            result["frameP99"] = it[2]; result["frameMax"] = it[3]
        }
    Regex("over16_7=(\\d+) over33_3=(\\d+) over50=(\\d+).+overheadMs=([0-9.]+)")
        .find(line)?.groupValues?.drop(1)?.map { it.toDouble() }?.let {
            result["over16_7"] = it[0]; result["over33_3"] = it[1]
            result["over50"] = it[2]; result["overheadMs"] = it[3]
        }
    return result
}
private val WINDOW_JSON = Json { encodeDefaults = true }
private val PERFORMANCE_WINDOW_UNITS = mapOf(
    "durationMs" to "ms",
    "fpsEffective" to "frames/s",
    "renderCpuMs" to "ms/frame (window mean)",
    "commandIdleMs" to "ms/frame (window mean)",
    "nonIdleMs" to "ms/frame (window mean)",
    "fenceWaitMs" to "ms/frame (window mean)",
    "commandBufferFenceWaitMs" to "ms/frame (window mean)",
    "asyncWaitMs" to "ms/frame (window mean)",
    "shaderCreateMs" to "ms/frame (window mean)",
    "queueSubmitCpuMs" to "ms/window",
    "acquireCpuMs" to "ms/window",
    "presentCallCpuMs" to "ms/window",
    "presentWaitMs" to "ms/window",
    "frameMs*" to "ms/frame",
    "frames" to "count/window",
    "gpuTimeMs" to "ms/window",
    "gpuTimePerFrameMs" to "ms/frame (completed samples mean)",
    "gpuFrameMs*" to "ms/frame (completed samples)",
    "gpuTimingCpuMs" to "ms/window (host instrumentation)",
    "gpuSamples" to "valid frames/window (completed results)",
    "gpuFramesObserved" to "completed frames/window (valid + dropped)",
    "gpuCoveragePct" to "percent",
    "*Calls" to "count/window",
)
