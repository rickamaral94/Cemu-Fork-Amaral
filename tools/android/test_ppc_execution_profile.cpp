#include "Cafe/HW/Espresso/Recompiler/PPCExecutionProfile.h"

#include <cassert>
#include <cstdio>
#include <thread>

int main()
{
	using namespace PPCExecutionProfile;
	Table table(2);
	assert(!table.GetOrCreate(0x1000));
	assert(!table.ReadAtQuiescence().enabled);
	table.Reset(true);
	auto* first = table.GetOrCreate(0x1000);
	assert(first && reinterpret_cast<std::uintptr_t>(first) % 64 == 0);
	assert(table.GetOrCreate(0x1000) == first);
	auto* second = table.GetOrCreate(0x2000);
	assert(second && !table.GetOrCreate(0x3000));
	assert(table.GetOrCreate(0x1000) == first); // stable even at capacity/recompilation
	std::array<std::thread, 3> writers;
	for (std::size_t core = 0; core < writers.size(); ++core)
	{
		writers[core] = std::thread([&, core] {
			for (int i = 0; i < 100000; ++i)
				++first->cores[core].executions;
		});
	}
	for (auto& writer : writers)
		writer.join();
	second->cores[2].executions = 4;
	const auto result = table.ReadAtQuiescence();
	assert(result.registeredEntries == 2 && result.uninstrumentedSegments == 1);
	assert(result.rows.size() == 2 && result.rows[0].address == 0x1000);
	assert(result.rows[0].total == 300000 && result.rows[0].cores[2] == 100000);
	assert(result.rows[1].total == 4);
	table.Reset(true); // a second title starts empty
	assert(table.ReadAtQuiescence().rows.empty());
	table.GetOrCreate(0x2000)->cores[0].executions = 1;
	table.GetOrCreate(0x1000)->cores[1].executions = 1;
	assert(table.ReadAtQuiescence().rows[0].address == 0x1000); // stable tie order
	table.Reset(false);
	assert(!table.GetOrCreate(0x1000) && table.ReadAtQuiescence().registeredEntries == 0);
	std::puts("PPC entry counters: PASS disabled, stable pointers, capacity, per-core writers, ranking and reset");
}
