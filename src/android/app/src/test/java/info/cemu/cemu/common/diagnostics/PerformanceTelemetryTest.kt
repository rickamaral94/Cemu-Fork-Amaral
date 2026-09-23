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
}
