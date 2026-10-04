#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

// Match the project's fixed-width aliases without pulling in the renderer/SDK.
using uint32 = uint32_t;
using uint64 = uint64_t;
#include "Cafe/HW/Latte/Renderer/Vulkan/GpuTimestampTracker.h"

int main()
{
	using Tracker = GpuTimestampTracker;
	assert(std::abs(*Tracker::DurationMs(100, 19300, 64, 52.083333333333) - 1.0) < 1e-9);
	assert(*Tracker::DurationMs(250, 5, 8, 1'000'000.0) == 11.0);
	assert(*Tracker::DurationMs(~uint64{0} - 4, 5, 64, 1'000'000.0) == 10.0);
	assert(!Tracker::DurationMs(0, 1, 0, 1.0));
	assert(!Tracker::DurationMs(0, 1, 65, 1.0));
	assert(!Tracker::DurationMs(0, 1, 64, 0.0));
	assert(!Tracker::DurationMs(0, 1, 64, std::numeric_limits<double>::quiet_NaN()));

	Tracker tracker;
	std::vector<std::optional<double>> results;
	const auto collect = [&](std::optional<double> ms) { results.push_back(ms); };
	const uint64 first = tracker.Submit();
	assert(tracker.Submit() == first);
	tracker.Complete(first, 1.25, collect);
	assert(results.empty()); // A result arriving before frame closure is retained.
	tracker.CloseFrame(collect);
	const uint64 second = tracker.Submit();
	tracker.CloseFrame(collect);
	tracker.Complete(second, 8.0, collect);
	assert(results.empty()); // A later frame must not overtake the pending first.
	tracker.Complete(first, 2.75, collect);
	assert(results.size() == 2 && *results[0] == 4.0 && *results[1] == 8.0);

	const uint64 invalid = tracker.Submit();
	tracker.Submit();
	tracker.CloseFrame(collect);
	tracker.Complete(invalid, 7.0, collect);
	tracker.Complete(invalid, std::nullopt, collect);
	assert(results.size() == 3 && !results.back()); // Never report partial frame time.
	tracker.CloseFrame(collect);
	assert(results.size() == 4 && !results.back()); // No submissions is not zero GPU time.

	for (uint32 frame = 0; frame < 100'000; ++frame)
	{
		const uint64 id = tracker.Submit();
		tracker.CloseFrame(collect);
		tracker.Complete(id, 3.5, collect);
		assert(*results.back() == 3.5);
	}
	assert(results.size() == 100'004);
	std::cout << "GPU timestamp conversion, wrapping, async frame aggregation, missing queries and recycling: PASS\n";
}
