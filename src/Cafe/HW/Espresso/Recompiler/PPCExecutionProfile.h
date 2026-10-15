#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

namespace PPCExecutionProfile
{
// One writer per virtual core. Read/reset only AFTER all guest CPUs and the
// compiler worker have stopped: generated code holds pointers into this table.
struct alignas(64) CoreCounter
{
	std::uint64_t executions = 0;
};
static_assert(sizeof(CoreCounter) == 64);
static_assert(offsetof(CoreCounter, executions) == 0);

struct EntryCounters
{
	std::array<CoreCounter, 3> cores{};
};

struct Row
{
	std::uint32_t address;
	std::array<std::uint64_t, 3> cores;
	std::uint64_t total;
};

struct Snapshot
{
	bool enabled;
	std::size_t registeredEntries;
	std::uint64_t uninstrumentedSegments;
	std::uint64_t allocationFailures;
	std::vector<Row> rows;
};

class Table
{
public:
	explicit Table(std::size_t capacity = 32768) : m_capacity(capacity) {}

	// Quiescent points only. Native code must be reclaimed before its counters.
	void Reset(bool enabled)
	{
		std::lock_guard lock(m_mutex);
		m_entries.clear();
		m_enabled = enabled;
		m_uninstrumentedSegments = 0;
		m_allocationFailures = 0;
	}

	// Compilation only, never called by generated code. Recompilations reuse
	// the entry's counters, including when old native code is still retained.
	EntryCounters* GetOrCreate(std::uint32_t address)
	{
		std::lock_guard lock(m_mutex);
		if (!m_enabled)
			return nullptr;
		const auto found = m_entries.find(address);
		if (found != m_entries.end())
			return found->second.get();
		if (m_entries.size() >= m_capacity)
		{
			++m_uninstrumentedSegments;
			return nullptr;
		}
		try
		{
			auto counters = std::make_unique<EntryCounters>();
			auto* result = counters.get();
			m_entries.emplace(address, std::move(counters));
			return result;
		}
		catch (const std::bad_alloc&)
		{
			++m_uninstrumentedSegments;
			++m_allocationFailures;
			return nullptr; // omit instrumentation, preserve normal translation
		}
	}

	// The mutex protects registration, NOT live execution counter reads.
	Snapshot ReadAtQuiescence() const
	{
		std::lock_guard lock(m_mutex);
		Snapshot result{m_enabled, m_entries.size(), m_uninstrumentedSegments, m_allocationFailures, {}};
		for (const auto& [address, counters] : m_entries)
		{
			Row row{address, {}, 0};
			for (std::size_t i = 0; i < row.cores.size(); ++i)
			{
				row.cores[i] = counters->cores[i].executions;
				row.total += row.cores[i];
			}
			if (row.total)
				result.rows.push_back(row);
		}
		std::sort(result.rows.begin(), result.rows.end(), [](const Row& a, const Row& b) {
			return a.total != b.total ? a.total > b.total : a.address < b.address;
		});
		return result;
	}

private:
	const std::size_t m_capacity;
	mutable std::mutex m_mutex;
	bool m_enabled = false;
	std::uint64_t m_uninstrumentedSegments = 0;
	std::uint64_t m_allocationFailures = 0;
	std::unordered_map<std::uint32_t, std::unique_ptr<EntryCounters>> m_entries;
};
}
