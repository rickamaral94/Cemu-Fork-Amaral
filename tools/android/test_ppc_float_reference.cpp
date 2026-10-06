#include <bit>
#include <cstdint>
#include <cstdio>

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
	return 0;
}
