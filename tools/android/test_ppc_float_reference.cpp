#include <bit>
#include <cstdint>
#include <cstdio>
#include <cfenv>

using uint8 = uint8_t;
using sint8 = int8_t;
using uint16 = uint16_t;
using sint16 = int16_t;
using uint32 = uint32_t;
using sint32 = int32_t;
using uint64 = uint64_t;

#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterHelper.h"
#include "Cafe/HW/Espresso/Recompiler/PPCRecompilerDifferentialValues.h"

int main()
{
	const int previousRounding = std::fegetround();
	if (std::fesetround(FE_TONEAREST) != 0)
		return 1;
	for (const auto& value : PPCRecompilerTestValues::kRoundedSingleValues)
	{
		volatile double input = std::bit_cast<double>(value.input);
		const uint64 actual = std::bit_cast<uint64>(static_cast<double>(static_cast<float>(input)));
		if (actual != value.expectedDouble)
		{
			std::fprintf(stderr, "frsp nearest-even literal mismatch\n");
			return 1;
		}
	}
	if (std::fesetround(previousRounding) != 0)
		return 1;
	for (const auto& value : PPCRecompilerTestValues::kSingleStoreValues)
	{
		const uint32 actual = ConvertToSingleNoFTZ(value.input);
		if (actual != value.expected)
		{
			std::fprintf(stderr, "stfs reference mismatch: input=%016llx expected=%08x actual=%08x\n",
				static_cast<unsigned long long>(value.input), value.expected, actual);
			return 1;
		}
	}
	// Demonstrate that the fixture catches the original rounded conversion.
	const auto& truncation = PPCRecompilerTestValues::kSingleStoreValues[0];
	const uint32 rounded = std::bit_cast<uint32>(static_cast<float>(std::bit_cast<double>(truncation.input)));
	if (rounded == truncation.expected)
		return 1;
	std::printf("stfs interpreter reference: PASS values=%zu roundingRegressionDetected=true\n",
		PPCRecompilerTestValues::kSingleStoreValues.size());
	std::printf("frsp nearest-even literal reference: PASS values=%zu\n", PPCRecompilerTestValues::kRoundedSingleValues.size());
	return 0;
}
