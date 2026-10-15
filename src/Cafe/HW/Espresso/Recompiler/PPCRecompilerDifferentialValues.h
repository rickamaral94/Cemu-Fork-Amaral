#pragma once

#include <array>
#include <cstdint>

namespace PPCRecompilerTestValues
{
// Bit transport/sign operations must preserve all remaining payload bits.
inline constexpr std::array<uint64_t, 8> kSignValues{
	0x0000000000000000ULL, 0x8000000000000000ULL,
	0x7FF0000000000000ULL, 0xFFF0000000000000ULL,
	0x7FF8002460000000ULL, 0xFFF0002460000000ULL,
	0x0000000000000001ULL, 0x8000000000000001ULL,
};

struct RoundedSingleValue
{
	uint64_t input;
	uint64_t expectedDouble;
};

// IEEE nearest-even boundaries with normal results (no host FTZ dependency).
inline constexpr std::array<RoundedSingleValue, 8> kRoundedSingleValues{
	RoundedSingleValue{0x3FF0000010000000ULL, 0x3FF0000000000000ULL}, // halfway, even lower
	RoundedSingleValue{0x3FF0000030000000ULL, 0x3FF0000040000000ULL}, // halfway, even upper
	RoundedSingleValue{0xBFF0000010000000ULL, 0xBFF0000000000000ULL},
	RoundedSingleValue{0xBFF0000030000000ULL, 0xBFF0000040000000ULL},
	RoundedSingleValue{0x0000000000000000ULL, 0x0000000000000000ULL},
	RoundedSingleValue{0x8000000000000000ULL, 0x8000000000000000ULL},
	RoundedSingleValue{0x47EFFFFFE0000000ULL, 0x47EFFFFFE0000000ULL}, // max finite single
	RoundedSingleValue{0xC7EFFFFFE0000000ULL, 0xC7EFFFFFE0000000ULL},
};

struct SingleStoreValue
{
	uint64_t input;
	uint32_t expected;
};

// Literal architectural results: do not derive these from the JIT or its helper.
inline constexpr std::array<SingleStoreValue, 16> kSingleStoreValues{
	SingleStoreValue{0x3FF0000018000000ULL, 0x3F800000}, // truncate, not round upward
	SingleStoreValue{0xBFF0000018000000ULL, 0xBF800000},
	SingleStoreValue{0x3FF000003FFFFFFFULL, 0x3F800001},
	SingleStoreValue{0x0000000000000000ULL, 0x00000000}, // +0
	SingleStoreValue{0x8000000000000000ULL, 0x80000000}, // -0
	SingleStoreValue{0x7FF0000000000000ULL, 0x7F800000}, // +infinity
	SingleStoreValue{0xFFF0000000000000ULL, 0xFF800000},
	SingleStoreValue{0x7FF8002460000000ULL, 0x7FC00123}, // quiet NaN, payload retained
	SingleStoreValue{0xFFF8002460000000ULL, 0xFFC00123},
	SingleStoreValue{0x7FF0002460000000ULL, 0x7F800123}, // signaling NaN, no conversion
	SingleStoreValue{0x36A0000000000000ULL, 0x00000001}, // smallest single subnormal
	SingleStoreValue{0xB6A0000000000000ULL, 0x80000001},
	SingleStoreValue{0x380FFFFFC0000000ULL, 0x007FFFFF}, // largest single subnormal
	SingleStoreValue{0x3810000000000000ULL, 0x00800000}, // smallest single normal
	SingleStoreValue{0xB800000000000000ULL, 0x80400000},
	SingleStoreValue{0x3FF4000000000000ULL, 0x3FA00000}, // exact 1.25
};
}
