package info.cemu.cemu.common.diagnostics

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Debug
import android.os.PowerManager
import android.os.Process
import android.os.SystemClock
import info.cemu.cemu.nativeinterface.NativeLogging
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.File
import java.util.Locale
import kotlin.math.max

private const val TELEMETRY_SAMPLE_INTERVAL_MS = 5_000L
private const val BYTES_PER_MEBIBYTE = 1024L * 1024L
private const val KGSL_BUSY_PATH = "/sys/class/kgsl/kgsl-3d0/gpubusy"
private const val KGSL_CLOCK_PATH = "/sys/class/kgsl/kgsl-3d0/gpuclk"
private const val CPU_PATH = "/sys/devices/system/cpu"

internal fun calculateProcessCpuPercent(
    cpuTimeDeltaMs: Long,
    elapsedTimeDeltaMs: Long,
): Double? {
    if (cpuTimeDeltaMs < 0 || elapsedTimeDeltaMs <= 0) {
        return null
    }
    return cpuTimeDeltaMs.toDouble() * 100.0 / elapsedTimeDeltaMs.toDouble()
}

internal fun parseKgslBusy(rawValue: String): Pair<Long, Long>? {
    val values = rawValue.trim().split(Regex("\\s+")).mapNotNull { it.toLongOrNull() }
    if (values.size < 2 || values[0] < 0 || values[1] <= 0) {
        return null
    }
    return values[0] to values[1]
}

internal fun calculateKgslBusyPercent(current: Pair<Long, Long>?): Double? {
    if (current == null) {
        return null
    }
    return (current.first.toDouble() * 100.0 / current.second.toDouble()).coerceIn(0.0, 100.0)
}

internal class PerformanceTelemetryLogger(private val context: Context) {
    private val activityManager = context.getSystemService(ActivityManager::class.java)
    private val powerManager = context.getSystemService(PowerManager::class.java)
    private val processorCount = max(Runtime.getRuntime().availableProcessors(), 1)
    private var samplingJob: Job? = null
    private var previousCpuTimeMs = 0L
    private var previousElapsedTimeMs = 0L
    private var sampleCount = 0
    private var cpuPercentSum = 0.0
    private var maxCpuPercent = 0.0
    private var maxPssMiB = 0L
    private var maxGraphicsMemoryMiB = 0L
    private var minAvailableMemoryMiB = Long.MAX_VALUE
    private var maxThermalStatus = PowerManager.THERMAL_STATUS_NONE
    private var maxBatteryTemperatureCelsius: Double? = null

    fun start(scope: CoroutineScope) {
        if (samplingJob != null) {
            return
        }
        previousCpuTimeMs = Process.getElapsedCpuTime()
        previousElapsedTimeMs = SystemClock.elapsedRealtime()
        NativeLogging.log("Android telemetry: event=start intervalMs=$TELEMETRY_SAMPLE_INTERVAL_MS")
        samplingJob = scope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(TELEMETRY_SAMPLE_INTERVAL_MS)
                runCatching { sample() }
                    .onFailure { NativeLogging.log("Android telemetry: event=sample-failed type=${it.javaClass.simpleName}") }
            }
        }
    }

    fun stop() {
        samplingJob?.cancel()
        samplingJob = null
        if (sampleCount == 0) {
            NativeLogging.log("Android telemetry summary: samples=0")
            return
        }
        val averageCpuPercent = cpuPercentSum / sampleCount
        NativeLogging.log(
            "Android telemetry summary: samples=$sampleCount " +
                    "cpuOneCoreAvgPct=${format(averageCpuPercent)} " +
                    "cpuOneCoreMaxPct=${format(maxCpuPercent)} maxPssMiB=$maxPssMiB " +
                    "maxGraphicsMemoryMiB=$maxGraphicsMemoryMiB " +
                    "minAvailableMemoryMiB=$minAvailableMemoryMiB " +
                    "maxThermalStatus=$maxThermalStatus " +
                    "maxBatteryTempC=${format(maxBatteryTemperatureCelsius)}",
        )
    }

    private fun sample() {
        val nowCpuTimeMs = Process.getElapsedCpuTime()
        val nowElapsedTimeMs = SystemClock.elapsedRealtime()
        val cpuPercent = calculateProcessCpuPercent(
            nowCpuTimeMs - previousCpuTimeMs,
            nowElapsedTimeMs - previousElapsedTimeMs,
        )
        previousCpuTimeMs = nowCpuTimeMs
        previousElapsedTimeMs = nowElapsedTimeMs

        val processMemory = activityManager.getProcessMemoryInfo(intArrayOf(Process.myPid())).firstOrNull()
        val pssMiB = processMemory?.totalPss?.toLong()?.div(1024L)
        val privateDirtyMiB = processMemory?.totalPrivateDirty?.toLong()?.div(1024L)
        val graphicsMemoryMiB = processMemory?.getMemoryStat("summary.graphics")
            ?.toLongOrNull()?.div(1024L)
        val javaHeapMiB = processMemory?.getMemoryStat("summary.java-heap")
            ?.toLongOrNull()?.div(1024L)
        val nativeHeapMiB = Debug.getNativeHeapAllocatedSize() / BYTES_PER_MEBIBYTE
        val systemMemory = ActivityManager.MemoryInfo().also(activityManager::getMemoryInfo)
        val availableMemoryMiB = systemMemory.availMem / BYTES_PER_MEBIBYTE
        val thermalStatus = powerManager.currentThermalStatus
        val batteryTemperatureCelsius = readBatteryTemperatureCelsius()
        val currentGpuBusy = readKgslBusy()
        val gpuBusyPercent = calculateKgslBusyPercent(currentGpuBusy)

        sampleCount++
        cpuPercent?.let {
            cpuPercentSum += it
            maxCpuPercent = max(maxCpuPercent, it)
        }
        pssMiB?.let { maxPssMiB = max(maxPssMiB, it) }
        graphicsMemoryMiB?.let { maxGraphicsMemoryMiB = max(maxGraphicsMemoryMiB, it) }
        minAvailableMemoryMiB = minOf(minAvailableMemoryMiB, availableMemoryMiB)
        maxThermalStatus = max(maxThermalStatus, thermalStatus)
        batteryTemperatureCelsius?.let { temperature ->
            maxBatteryTemperatureCelsius = maxBatteryTemperatureCelsius?.let { max(it, temperature) }
                ?: temperature
        }

        NativeLogging.log(
            "Android telemetry: event=sample " +
                    "cpuOneCorePct=${format(cpuPercent)} " +
                    "cpuNormalizedPct=${format(cpuPercent?.div(processorCount))} " +
                    "pssMiB=${pssMiB ?: "unavailable"} " +
                    "privateDirtyMiB=${privateDirtyMiB ?: "unavailable"} " +
                    "graphicsMemoryMiB=${graphicsMemoryMiB ?: "unavailable"} " +
                    "javaHeapMiB=${javaHeapMiB ?: "unavailable"} nativeHeapMiB=$nativeHeapMiB " +
                    "availableMemoryMiB=$availableMemoryMiB lowMemory=${systemMemory.lowMemory} " +
                    "thermalStatus=$thermalStatus batteryTempC=${format(batteryTemperatureCelsius)} " +
                    "cpuClockMHz=${readCpuClockMHz()} gpuClockMHz=${readGpuClockMHz() ?: "unavailable"} " +
                    "gpuBusyPct=${format(gpuBusyPercent)}",
        )
    }

    private fun readBatteryTemperatureCelsius(): Double? {
        val battery = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            ?: return null
        val tenthsCelsius = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
        return if (tenthsCelsius == Int.MIN_VALUE) null else tenthsCelsius / 10.0
    }

    private fun readCpuClockMHz(): String {
        val clocks = File(CPU_PATH).listFiles { file ->
            file.isDirectory && file.name.matches(Regex("cpu\\d+"))
        }?.sortedBy { it.name.removePrefix("cpu").toIntOrNull() ?: Int.MAX_VALUE }
            ?.mapNotNull { cpu -> readLong(cpu.resolve("cpufreq/scaling_cur_freq"))?.div(1000L) }
            .orEmpty()
        return if (clocks.isEmpty()) "unavailable" else clocks.joinToString(",", prefix = "[", postfix = "]")
    }

    private fun readKgslBusy(): Pair<Long, Long>? = runCatching {
        parseKgslBusy(File(KGSL_BUSY_PATH).readText())
    }.getOrNull()

    private fun readGpuClockMHz(): Long? = readLong(File(KGSL_CLOCK_PATH))?.div(1_000_000L)

    private fun readLong(file: File): Long? = runCatching { file.readText().trim().toLong() }.getOrNull()

    private fun format(value: Double?): String = value?.let {
        String.format(Locale.ROOT, "%.2f", it)
    } ?: "unavailable"
}
