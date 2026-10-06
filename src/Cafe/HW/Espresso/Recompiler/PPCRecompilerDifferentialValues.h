#pragma once

#include <array>
#include <cstdint>

namespace PPCRecompilerTestValues
{
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
