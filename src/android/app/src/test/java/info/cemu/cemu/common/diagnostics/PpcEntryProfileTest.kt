package info.cemu.cemu.common.diagnostics

import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Assert.*
import org.junit.Test

class PpcEntryProfileTest {
    private val summary = "JIT PPC entry profile v1: enabled=true metric=entry-executions scope=enterable-iml-segments registeredEntries=2 executedEntries=2 totalExecutions=14 uninstrumentedSegments=0 allocationFailures=0 complete=true readback=quiescent overhead=unmeasured"
    private val row1 = "JIT PPC entry profile row v1: rank=1 pc=0x02001000 executions=10 core0=2 core1=7 core2=1"
    private val row2 = "JIT PPC entry profile row v1: rank=2 pc=0x02002000 executions=4 core0=0 core1=4 core2=0"

    @Test fun parsesQuiescentRankingWithUnitsAndCoreCounts() {
        val profile = extractPpcEntryProfile("$summary\n$row1\n$row2")
        assertEquals("complete", profile.status)
        assertEquals(true, profile.enabled)
        assertEquals(7L, profile.topEntries[0].core1)
        assertEquals("entry-executions", profile.metric)
        assertTrue(Json.encodeToString(profile).contains("0x02001000"))
    }

    @Test fun activeAndLegacyLogsDoNotPretendToHaveZeroCounts() {
        assertEquals("unavailable", extractPpcEntryProfile("old log").status)
        assertNull(extractPpcEntryProfile("old log").enabled)
        assertEquals("awaiting-shutdown", extractPpcEntryProfile("JIT PPC entry profile config v1: enabled=true").status)
        assertEquals("disabled", extractPpcEntryProfile("JIT PPC entry profile config v1: enabled=false").status)
    }

    @Test fun marksCapacityExhaustionAndTruncatedRanking() {
        val partial = summary.replace("complete=true", "complete=false").replace("uninstrumentedSegments=0", "uninstrumentedSegments=3")
        assertEquals("partial", extractPpcEntryProfile("$partial\n$row1\n$row2").status)
        assertEquals("incomplete", extractPpcEntryProfile("$summary\n$row1").status)
    }

    @Test fun rejectsInvalidCoreSumsDuplicateRowsAndOverflow() {
        for (bad in listOf(row2.replace("core1=4", "core1=5"), row1,
            row2.replace("core1=4", "core1=9223372036854775807"))) {
            assertEquals("incomplete", extractPpcEntryProfile("$summary\n$row1\n$bad").status)
        }
    }

    @Test fun newSessionCannotInheritPreviousTitleRanking() {
        val log = "$summary\n$row1\n$row2\nMounting title 000500001010f800\nJIT PPC entry profile config v1: enabled=true"
        val profile = extractPpcEntryProfile(log)
        assertEquals("awaiting-shutdown", profile.status)
        assertTrue(profile.topEntries.isEmpty())
    }

    @Test fun disabledSummaryAndEmptyRankingAreValid() {
        val disabled = summary.replace("enabled=true", "enabled=false").replace("executedEntries=2", "executedEntries=0")
            .replace("totalExecutions=14", "totalExecutions=0").replace("registeredEntries=2", "registeredEntries=0")
        assertEquals("disabled", extractPpcEntryProfile(disabled).status)
    }
}
