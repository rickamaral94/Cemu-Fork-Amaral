#pragma once

#include <cmath>
#include <deque>
#include <optional>

// GPU-thread only. Publish a frame after all its submitted segments retire.
// An unavailable segment invalidates the frame instead of undercounting it.
class GpuTimestampTracker
{
public:
	static std::optional<double> DurationMs(uint64 begin, uint64 end, uint32 validBits, double periodNs)
	{
		if (validBits == 0 || validBits > 64 || !std::isfinite(periodNs) || periodNs <= 0.0)
			return std::nullopt;
		const uint64 mask = validBits == 64 ? ~uint64{0} : (uint64{1} << validBits) - 1;
		return static_cast<double>((end - begin) & mask) * periodNs / 1'000'000.0;
	}

	uint64 Submit()
	{
		auto& frame = m_frames.back();
		++frame.pending;
		++frame.submits;
		return frame.id;
	}

	template<typename Callback>
	void Complete(uint64 frameId, std::optional<double> durationMs, Callback callback)
	{
		auto& frame = m_frames.at(static_cast<size_t>(frameId - m_frames.front().id));
		--frame.pending;
		if (durationMs && std::isfinite(*durationMs) && *durationMs >= 0.0)
			frame.timeMs += *durationMs;
		else
			frame.valid = false;
		Drain(callback);
	}

	template<typename Callback>
	void CloseFrame(Callback callback)
	{
		m_frames.back().closed = true;
		const uint64 nextId = m_frames.back().id + 1;
		m_frames.push_back(Frame{nextId});
		Drain(callback);
	}

private:
	struct Frame
	{
		uint64 id{};
		uint32 pending{};
		uint32 submits{};
		double timeMs{};
		bool valid{true};
		bool closed{};
	};

	template<typename Callback>
	void Drain(Callback callback)
	{
		while (m_frames.front().closed && m_frames.front().pending == 0)
		{
			const auto& frame = m_frames.front();
			callback(frame.valid && frame.submits > 0 ? std::optional<double>{frame.timeMs} : std::nullopt);
			m_frames.pop_front();
		}
	}

	std::deque<Frame> m_frames{Frame{}};
};
