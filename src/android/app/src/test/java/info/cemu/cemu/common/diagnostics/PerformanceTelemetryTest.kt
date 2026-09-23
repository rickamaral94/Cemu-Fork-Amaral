package info.cemu.cemu.common.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PerformanceTelemetryTest {
    @Test
    fun calculatesProcessCpuAsPercentageOfOneCore() {
        assertEquals(150.0, calculateProcessCpuPercent(750, 500)!!, 0.001)
        assertNull(calculateProcessCpuPercent(100, 0))
    }

    @Test
    fun parsesKgslBusyCounters() {
        assertEquals(120L to 500L, parseKgslBusy("120 500\n"))
        assertNull(parseKgslBusy("unavailable"))
    }

    @Test
    fun calculatesKgslBusyFromReportedWindow() {
        assertEquals(50.0, calculateKgslBusyPercent(150L to 300L)!!, 0.001)
        assertNull(calculateKgslBusyPercent(null))
    }

    @Test
    fun parsesLinuxThreadCpuStatWithSpacesInName() {
        val stat = "42 (Cemu render worker) S 1 2 3 4 5 6 7 8 9 10 120 30 0 0"
        assertEquals(ThreadCpuStat(42, "RenderThread", 150), parseProcThreadStat(stat, "RenderThread\n"))
    }

    @Test
    fun rejectsIncompleteLinuxThreadCpuStat() {
        assertNull(parseProcThreadStat("42 (broken) S 1 2", "broken"))
    }
}
