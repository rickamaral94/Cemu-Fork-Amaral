package info.cemu.cemu.common.diagnostics

import kotlinx.serialization.Serializable

@Serializable
data class PpcEntryProfileRow(
    val rank: Int,
    val pc: String,
    val executions: Long,
    val core0: Long,
    val core1: Long,
    val core2: Long,
)

@Serializable
data class PpcEntryProfile(
    val status: String = "unavailable",
    val enabled: Boolean? = null,
    val metric: String = "entry-executions",
    val scope: String = "enterable-iml-segments",
    val summary: Map<String, String> = emptyMap(),
    val topEntries: List<PpcEntryProfileRow> = emptyList(),
    val note: String = "Execution counts are not CPU time or function-call counts. Profiling overhead is unmeasured. Ranking requires normal game shutdown.",
)

internal fun extractPpcEntryProfile(log: String): PpcEntryProfile {
    var enabled: Boolean? = null
    var summary = emptyMap<String, String>()
    val rows = mutableListOf<PpcEntryProfileRow>()
    val pairs = Regex("(\\w+)=([^\\s]+)")
    fun values(line: String) = pairs.findAll(line).associate { it.groupValues[1] to it.groupValues[2] }
    log.lineSequence().forEach { line ->
        when {
            line.contains("Mounting title ") || line.contains("------- Init Cemu ") -> {
                enabled = null
                summary = emptyMap()
                rows.clear()
            }
            line.contains("JIT PPC entry profile config v1:") -> {
                enabled = values(line)["enabled"]?.toBooleanStrictOrNull()
                summary = emptyMap()
                rows.clear()
            }
            line.contains("JIT PPC entry profile v1:") -> {
                summary = values(line)
                enabled = summary["enabled"]?.toBooleanStrictOrNull()
                rows.clear()
            }
            line.contains("JIT PPC entry profile row v1:") && summary.isNotEmpty() -> {
                val v = values(line)
                val rank = v["rank"]?.toIntOrNull() ?: return@forEach
                val pc = v["pc"]?.takeIf { it.matches(Regex("0x[0-9a-fA-F]{8}")) }?.lowercase() ?: return@forEach
                val count = v["executions"]?.toLongOrNull()?.takeIf { it > 0 } ?: return@forEach
                val cores = listOf("core0", "core1", "core2").map { key ->
                    v[key]?.toLongOrNull()?.takeIf { it >= 0 } ?: return@forEach
                }
                if (rank !in 1..32 || cores.any { it > count } || cores[0] > count - cores[1] ||
                    cores[0] + cores[1] != count - cores[2] || rows.any { it.rank == rank || it.pc == pc }) return@forEach
                rows += PpcEntryProfileRow(rank, pc, count, cores[0], cores[1], cores[2])
            }
        }
    }
    if (summary.isEmpty()) return PpcEntryProfile(
        status = if (enabled == true) "awaiting-shutdown" else if (enabled == false) "disabled" else "unavailable",
        enabled = enabled,
    )
    val entries = summary["executedEntries"]?.toLongOrNull()
    val registered = summary["registeredEntries"]?.toLongOrNull()
    val total = summary["totalExecutions"]?.toLongOrNull()
    val omitted = summary["uninstrumentedSegments"]?.toLongOrNull()
    val failures = summary["allocationFailures"]?.toLongOrNull()
    var remainder = total ?: -1
    rows.forEach { row -> remainder = if (remainder >= row.executions) remainder - row.executions else -1 }
    val rankingPresent = entries != null && entries >= 0 && rows.size.toLong() == minOf(entries, 32) &&
            rows.sortedBy { it.rank }.map { it.rank } == (1..rows.size).toList() &&
            remainder >= 0 && (entries > 32 || remainder == 0L)
    val validSummary = summary["readback"] == "quiescent" && summary["metric"] == "entry-executions" &&
            summary["scope"] == "enterable-iml-segments" && registered != null && entries != null &&
            registered >= entries && total != null && total >= 0 && omitted != null && omitted >= 0 &&
            failures != null && failures >= 0 && failures <= omitted &&
            summary["complete"] == (omitted == 0L).toString()
    val status = when {
        !rankingPresent || !validSummary || enabled == null -> "incomplete"
        enabled == false && entries == 0L -> "disabled"
        enabled == false -> "incomplete"
        summary["complete"] == "true" -> "complete"
        else -> "partial"
    }
    return PpcEntryProfile(status, enabled, summary = summary, topEntries = rows.sortedBy { it.rank })
}
