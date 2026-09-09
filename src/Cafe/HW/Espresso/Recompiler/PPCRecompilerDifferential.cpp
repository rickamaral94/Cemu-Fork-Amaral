#include "PPCRecompiler.h"

#include "BackendAArch64/BackendAArch64.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Espresso/Recompiler/PPCFunctionBoundaryTracker.h"
#include "Cafe/HW/MMU/MMU.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cemu/Logging/CemuLogging.h"

PPCRecFunction_t* PPCRecompiler_recompileFunction(PPCFunctionBoundaryTracker::PPCRange_t range,
	std::set<uint32>& entryAddresses, std::vector<std::pair<MPTR, uint32>>& entryPointsOut,
	PPCFunctionBoundaryTracker& boundaryTracker, bool applyConfiguredRange);

namespace
{
constexpr uint32 kReturnToInterpreter = 0x4E800020; // blr
constexpr uint32 kTestAllocationSize = 256;
constexpr uint32 kTestDataOffset = 128;

constexpr uint32 EncodeD(uint32 primaryOpcode, uint32 rtOrRs, uint32 ra, sint32 immediate)
{
	return (primaryOpcode << 26) | (rtOrRs << 21) | (ra << 16) |
		static_cast<uint16>(immediate);
}

constexpr uint32 EncodeX(uint32 primaryOpcode, uint32 rtOrRs, uint32 ra, uint32 rb, uint32 xo, bool record = false)
{
	return (primaryOpcode << 26) | (rtOrRs << 21) | (ra << 16) | (rb << 11) |
		(xo << 1) | static_cast<uint32>(record);
}

constexpr uint32 EncodeRotate(uint32 rs, uint32 ra, uint32 shift, uint32 maskBegin, uint32 maskEnd)
{
	return (21U << 26) | (rs << 21) | (ra << 16) | (shift << 11) |
		(maskBegin << 6) | (maskEnd << 1);
}

constexpr uint32 EncodeFloat(uint32 primaryOpcode, uint32 fd, uint32 fa, uint32 fb, uint32 xo)
{
	return (primaryOpcode << 26) | (fd << 21) | (fa << 16) | (fb << 11) | (xo << 1);
}

constexpr uint32 EncodeFloatMultiply(uint32 fd, uint32 fa, uint32 fc)
{
	return (63U << 26) | (fd << 21) | (fa << 16) | (fc << 6) | (25U << 1);
}

static_assert(EncodeX(31, 0, 4, 30, 316) == 0x7C04F278);      // xor r4, r0, r30
static_assert(EncodeRotate(4, 8, 7, 0, 31) == 0x5488383E);    // rotlwi r8, r4, 7
static_assert(EncodeFloat(63, 1, 9, 10, 21) == 0xFC29502A);  // fadd f1, f9, f10
static_assert(EncodeFloatMultiply(10, 11, 12) == 0xFD4B0332); // fmul f10, f11, f12

constexpr std::array<uint32, 8> kIntegerCode{
	EncodeD(14, 3, 0, 0x1234),          // li r3, 0x1234
	EncodeD(14, 4, 3, -0x34),           // addi r4, r3, -0x34
	EncodeX(31, 5, 3, 4, 266),          // add r5, r3, r4
	EncodeX(31, 5, 6, 3, 316),          // xor r6, r5, r3
	EncodeD(24, 6, 7, 0x00FF),          // ori r7, r6, 0x00ff
	EncodeD(28, 7, 8, 0x0F0F),          // andi. r8, r7, 0x0f0f
	EncodeRotate(3, 9, 8, 0, 31),       // rotlwi r9, r3, 8
	kReturnToInterpreter,
};

constexpr std::array<uint32, 7> kBranchCode{
	EncodeD(14, 3, 0, -1),              // li r3, -1
	0x2C030000,                          // cmpwi r3, 0
	0x4180000C,                          // blt +12
	EncodeD(14, 4, 0, 2),               // li r4, 2
	0x48000008,                          // b +8
	EncodeD(14, 4, 0, 1),               // li r4, 1
	kReturnToInterpreter,
};

constexpr std::array<uint32, 4> kLoadStoreCode{
	EncodeD(32, 4, 3, 0),               // lwz r4, 0(r3)
	EncodeD(14, 4, 4, 1),               // addi r4, r4, 1
	EncodeD(36, 4, 3, 4),               // stw r4, 4(r3)
	kReturnToInterpreter,
};

constexpr std::array<uint32, 4> kFloatingPointCode{
	EncodeFloat(63, 3, 1, 2, 21),       // fadd f3, f1, f2
	EncodeFloatMultiply(4, 3, 2),        // fmul f4, f3, f2
	EncodeFloat(4, 5, 1, 2, 21),        // ps_add f5, f1, f2
	kReturnToInterpreter,
};

constexpr std::array<uint32, 4> kAtomicSuccessCode{
	EncodeX(31, 4, 0, 3, 20),           // lwarx r4, 0, r3
	EncodeD(14, 5, 4, 1),                // addi r5, r4, 1
	EncodeX(31, 5, 0, 3, 150, true),    // stwcx. r5, 0, r3
	kReturnToInterpreter,
};

constexpr std::array<uint32, 5> kAtomicCompareFailureCode{
	EncodeX(31, 4, 0, 3, 20),           // lwarx r4, 0, r3
	EncodeD(14, 5, 4, 1),                // addi r5, r4, 1
	EncodeD(36, 6, 3, 0),                // stw r6, 0(r3)
	EncodeX(31, 5, 0, 3, 150, true),    // stwcx. r5, 0, r3
	kReturnToInterpreter,
};

static_assert(kAtomicSuccessCode[0] == 0x7C801828); // lwarx r4, 0, r3
static_assert(kAtomicSuccessCode[2] == 0x7CA0192D); // stwcx. r5, 0, r3

using StateSetup = void (*)(PPCInterpreter_t&, MPTR);
using MemorySetup = void (*)(MPTR);
using ResultValidator = std::string (*)(const PPCInterpreter_t&, MPTR);

struct DifferentialCase
{
	const char* name;
	std::span<const uint32> code;
	StateSetup setupState;
	MemorySetup setupMemory;
	ResultValidator validate;
	bool compareDataWord;
};

void SetupNoState(PPCInterpreter_t&, MPTR)
{
}

void SetupLoadStore(PPCInterpreter_t& state, MPTR dataAddress)
{
	state.gpr[3] = dataAddress;
}

void SetupFloatingPoint(PPCInterpreter_t& state, MPTR)
{
	state.fpr[1].fp0 = 1.25;
	state.fpr[1].fp1 = -2.0;
	state.fpr[2].fp0 = 0.75;
	state.fpr[2].fp1 = 4.0;
}

void SetupAtomicSuccess(PPCInterpreter_t& state, MPTR dataAddress)
{
	state.gpr[3] = dataAddress;
	state.cr[0] = 1;
	state.cr[1] = 1;
	state.cr[2] = 0;
	state.cr[3] = 0;
	state.xer_so = 1;
}

void SetupAtomicCompareFailure(PPCInterpreter_t& state, MPTR dataAddress)
{
	state.gpr[3] = dataAddress;
	state.gpr[6] = 0xA1B2C3D4;
	state.cr[0] = 1;
	state.cr[1] = 1;
	state.cr[2] = 1;
	state.cr[3] = 1;
	state.xer_so = 0;
}

void PrepareLoadStoreMemory(MPTR dataAddress)
{
	memory_writeU32(dataAddress, 0x11223344);
	memory_writeU32(dataAddress + 4, 0xDEADBEEF);
}

void PrepareAtomicMemory(MPTR dataAddress)
{
	memory_writeU32(dataAddress, 0x10203040);
}

std::string ValidateInteger(const PPCInterpreter_t& state, MPTR)
{
	if (state.gpr[3] != 0x1234 || state.gpr[4] != 0x1200 || state.gpr[5] != 0x2434 ||
		state.gpr[6] != 0x3600 || state.gpr[7] != 0x36FF || state.gpr[8] != 0x060F ||
		state.gpr[9] != 0x00123400)
	{
		return fmt::format("unexpected integer result r3={:08x} r4={:08x} r5={:08x} r6={:08x} r7={:08x} r8={:08x} r9={:08x}",
			state.gpr[3], state.gpr[4], state.gpr[5], state.gpr[6], state.gpr[7], state.gpr[8], state.gpr[9]);
	}
	return {};
}

std::string ValidateBranch(const PPCInterpreter_t& state, MPTR)
{
	if (state.gpr[4] != 1 || state.cr[0] != 1 || state.cr[1] != 0 || state.cr[2] != 0)
		return fmt::format("unexpected branch/CR result r4={:08x} cr0=[{},{},{},{}]",
			state.gpr[4], state.cr[0], state.cr[1], state.cr[2], state.cr[3]);
	return {};
}

std::string ValidateLoadStore(const PPCInterpreter_t& state, MPTR dataAddress)
{
	const uint32 storedValue = memory_readU32(dataAddress + 4);
	if (state.gpr[4] != 0x11223345 || storedValue != 0x11223345)
		return fmt::format("unexpected load/store result r4={:08x} memory={:08x}", state.gpr[4], storedValue);
	return {};
}

std::string ValidateFloatingPoint(const PPCInterpreter_t& state, MPTR)
{
	if (state.fpr[3].fp0 != 2.0 || state.fpr[4].fp0 != 1.5 ||
		state.fpr[5].fp0 != 2.0 || state.fpr[5].fp1 != 2.0)
	{
		return fmt::format("unexpected FP/PS result f3={} f4={} ps5=[{},{}]",
			state.fpr[3].fp0, state.fpr[4].fp0, state.fpr[5].fp0, state.fpr[5].fp1);
	}
	return {};
}

std::string ValidateAtomicSuccess(const PPCInterpreter_t& state, MPTR dataAddress)
{
	const uint32 storedValue = memory_readU32(dataAddress);
	if (state.gpr[4] != 0x10203040 || state.gpr[5] != 0x10203041 ||
		storedValue != 0x10203041 || state.reservedMemAddr != 0 ||
		state.reservedMemValue != 0 || state.cr[0] != 0 || state.cr[1] != 0 ||
		state.cr[2] != 1 || state.cr[3] != 1)
	{
		return fmt::format(
			"unexpected atomic-success result r4={:08x} r5={:08x} memory={:08x} reservation=[{:08x},{:08x}] cr0=[{},{},{},{}]",
			state.gpr[4], state.gpr[5], storedValue, state.reservedMemAddr,
			state.reservedMemValue, state.cr[0], state.cr[1], state.cr[2], state.cr[3]);
	}
	return {};
}

std::string ValidateAtomicCompareFailure(const PPCInterpreter_t& state, MPTR dataAddress)
{
	const uint32 storedValue = memory_readU32(dataAddress);
	if (state.gpr[4] != 0x10203040 || state.gpr[5] != 0x10203041 ||
		storedValue != 0xA1B2C3D4 || state.reservedMemAddr != 0 ||
		state.reservedMemValue != 0 || state.cr[0] != 0 || state.cr[1] != 0 ||
		state.cr[2] != 0 || state.cr[3] != 0)
	{
		return fmt::format(
			"unexpected atomic-failure result r4={:08x} r5={:08x} memory={:08x} reservation=[{:08x},{:08x}] cr0=[{},{},{},{}]",
			state.gpr[4], state.gpr[5], storedValue, state.reservedMemAddr,
			state.reservedMemValue, state.cr[0], state.cr[1], state.cr[2], state.cr[3]);
	}
	return {};
}

std::string CompareArchitecturalState(const PPCInterpreter_t& interpreter, const PPCInterpreter_t& jit)
{
	for (size_t i = 0; i < std::size(interpreter.gpr); ++i)
	{
		if (interpreter.gpr[i] != jit.gpr[i])
			return fmt::format("GPR{} interpreter={:08x} jit={:08x}", i, interpreter.gpr[i], jit.gpr[i]);
	}
	for (size_t i = 0; i < std::size(interpreter.fpr); ++i)
	{
		if (interpreter.fpr[i].fp0int != jit.fpr[i].fp0int || interpreter.fpr[i].fp1int != jit.fpr[i].fp1int)
			return fmt::format("FPR{} differs", i);
	}
	for (size_t i = 0; i < std::size(interpreter.cr); ++i)
	{
		if (interpreter.cr[i] != jit.cr[i])
			return fmt::format("CR bit {} interpreter={} jit={}", i, interpreter.cr[i], jit.cr[i]);
	}
	if (interpreter.instructionPointer != jit.instructionPointer)
		return fmt::format("instruction pointer interpreter={:08x} jit={:08x}", interpreter.instructionPointer, jit.instructionPointer);
	if (interpreter.fpscr != jit.fpscr || interpreter.xer_ca != jit.xer_ca ||
		interpreter.xer_so != jit.xer_so || interpreter.xer_ov != jit.xer_ov)
		return "FPSCR/XER differs";
	if (interpreter.spr.LR != jit.spr.LR || interpreter.spr.CTR != jit.spr.CTR ||
		interpreter.spr.XER != jit.spr.XER || interpreter.spr.UPIR != jit.spr.UPIR)
		return "LR/CTR/XER/UPIR SPR differs";
	for (size_t i = 0; i < std::size(interpreter.spr.UGQR); ++i)
	{
		if (interpreter.spr.UGQR[i] != jit.spr.UGQR[i])
			return fmt::format("UGQR{} interpreter={:08x} jit={:08x}", i, interpreter.spr.UGQR[i], jit.spr.UGQR[i]);
	}
	if (interpreter.reservedMemAddr != jit.reservedMemAddr || interpreter.reservedMemValue != jit.reservedMemValue)
		return "reservation state differs";
	if (interpreter.LSQE != jit.LSQE || interpreter.PSE != jit.PSE)
		return "paired-single mode differs";
	if (interpreter.memoryException != jit.memoryException)
		return "memory exception state differs";
	return {};
}

bool ExecuteInterpreter(PPCInterpreter_t& state, size_t instructionLimit)
{
	// Branch instructions executed by the interpreter normally notify the JIT.
	// The test terminates with `blr` to LR=0, which is the recompiler escape
	// address. Letting that notification through marks address zero as visited
	// and queues it for asynchronous compilation after the test, even though it
	// is not guest code. Keep the reference run isolated from all JIT state.
	PPCRecompiler_Disable();
	PPCInterpreter_t* previousInstance = PPCInterpreter_getCurrentInstance();
	PPCInterpreter_setCurrentInstance(&state);
	for (size_t i = 0; i < instructionLimit && state.instructionPointer != 0; ++i)
		PPCInterpreterSlim_executeInstruction(&state);
	PPCInterpreter_setCurrentInstance(previousInstance);
	PPCRecompiler_Enable();
	return state.instructionPointer == 0;
}

bool ExecuteJit(PPCInterpreter_t& state, void* entryPoint)
{
	PPCInterpreter_t* previousInstance = PPCInterpreter_getCurrentInstance();
	PPCInterpreter_setCurrentInstance(&state);
	PPCRecompiler_enterRecompilerCode(reinterpret_cast<uint64>(entryPoint), reinterpret_cast<uint64>(&state));
	PPCInterpreter_setCurrentInstance(previousInstance);
	return state.instructionPointer == 0;
}

PPCRecFunction_t* CompileTestFunction(MPTR codeAddress, void*& entryPoint)
{
	PPCFunctionBoundaryTracker boundaryTracker;
	boundaryTracker.trackStartPoint(codeAddress);
	PPCFunctionBoundaryTracker::PPCRange_t range;
	if (!boundaryTracker.getRangeForAddress(codeAddress, range))
		return nullptr;

	std::set<uint32> entryAddresses{codeAddress};
	std::vector<std::pair<MPTR, uint32>> entryPoints;
	PPCRecFunction_t* function = PPCRecompiler_recompileFunction(range, entryAddresses, entryPoints, boundaryTracker, false);
	if (!function)
		return nullptr;

	for (const auto& [ppcAddress, hostOffset] : entryPoints)
	{
		if (ppcAddress == codeAddress)
		{
			entryPoint = static_cast<uint8*>(function->x86Code) + hostOffset;
			return function;
		}
	}

	PPCRecompiler_cleanupAArch64Code(function->x86Code, function->x86Size);
	delete function;
	return nullptr;
}

bool RunCase(const DifferentialCase& testCase, MPTR codeAddress, MPTR dataAddress)
{
	for (size_t i = 0; i < testCase.code.size(); ++i)
		memory_writeU32(codeAddress + static_cast<MPTR>(i * sizeof(uint32)), testCase.code[i]);

	void* entryPoint = nullptr;
	PPCRecFunction_t* function = CompileTestFunction(codeAddress, entryPoint);
	if (!function)
	{
		cemuLog_log(LogType::Force, "JIT ARM64 differential: case={} result=FAIL reason=compile", testCase.name);
		return false;
	}

	PPCInterpreterGlobal_t globalState{};
	PPCInterpreter_t initialState{};
	initialState.instructionPointer = codeAddress;
	initialState.spr.LR = 0;
	initialState.remainingCycles = 1000000;
	initialState.LSQE = 1;
	initialState.PSE = 1;
	initialState.global = &globalState;
	testCase.setupState(initialState, dataAddress);

	PPCInterpreter_t interpreterState = initialState;
	PPCInterpreter_t jitState = initialState;
	if (testCase.setupMemory)
		testCase.setupMemory(dataAddress);
	const bool interpreterExited = ExecuteInterpreter(interpreterState, testCase.code.size() * 4 + 16);
	const uint32 interpreterDataWord = testCase.compareDataWord ? memory_readU32(dataAddress + 4) : 0;
	const std::string interpreterValidation = testCase.validate(interpreterState, dataAddress);

	if (testCase.setupMemory)
		testCase.setupMemory(dataAddress);
	const bool jitExited = ExecuteJit(jitState, entryPoint);
	const uint32 jitDataWord = testCase.compareDataWord ? memory_readU32(dataAddress + 4) : 0;
	const std::string jitValidation = testCase.validate(jitState, dataAddress);

	const std::string stateDifference = CompareArchitecturalState(interpreterState, jitState);
	const bool memoryMatches = !testCase.compareDataWord || interpreterDataWord == jitDataWord;
	const bool passed = interpreterExited && jitExited && interpreterValidation.empty() &&
		jitValidation.empty() && stateDifference.empty() && memoryMatches;

	if (!passed)
	{
		std::string reason;
		if (!interpreterExited)
			reason = "interpreter did not exit";
		else if (!jitExited)
			reason = "JIT did not exit";
		else if (!interpreterValidation.empty())
			reason = "interpreter: " + interpreterValidation;
		else if (!jitValidation.empty())
			reason = "JIT: " + jitValidation;
		else if (!stateDifference.empty())
			reason = stateDifference;
		else
			reason = fmt::format("memory interpreter={:08x} jit={:08x}", interpreterDataWord, jitDataWord);
		cemuLog_log(LogType::Force, "JIT ARM64 differential: case={} result=FAIL reason={}", testCase.name, reason);
	}

	PPCRecompiler_cleanupAArch64Code(function->x86Code, function->x86Size);
	delete function;
	return passed;
}
}

void PPCRecompiler_RunAArch64DifferentialTests()
{
	const MEMPTR<void> allocation = RPLLoader_AllocateCodeCaveMem(256, kTestAllocationSize);
	if (!allocation)
	{
		cemuLog_log(LogType::Force, "JIT ARM64 differential: result=SKIP reason=no-code-cave-memory");
		return;
	}

	const MPTR codeAddress = allocation.GetMPTR();
	const MPTR dataAddress = codeAddress + kTestDataOffset;
	const std::array<DifferentialCase, 6> cases{
		DifferentialCase{"integer-cr-rotate", kIntegerCode, SetupNoState, nullptr, ValidateInteger, false},
		DifferentialCase{"conditional-branch", kBranchCode, SetupNoState, nullptr, ValidateBranch, false},
		DifferentialCase{"load-store-endian", kLoadStoreCode, SetupLoadStore, PrepareLoadStoreMemory, ValidateLoadStore, true},
		DifferentialCase{"floating-paired-single", kFloatingPointCode, SetupFloatingPoint, nullptr, ValidateFloatingPoint, false},
		DifferentialCase{"atomic-reservation-success", kAtomicSuccessCode, SetupAtomicSuccess, PrepareAtomicMemory, ValidateAtomicSuccess, false},
		DifferentialCase{"atomic-reservation-compare-failure", kAtomicCompareFailureCode, SetupAtomicCompareFailure, PrepareAtomicMemory, ValidateAtomicCompareFailure, false},
	};

	uint32 passedCount = 0;
	for (const auto& testCase : cases)
	{
		if (RunCase(testCase, codeAddress, dataAddress))
			passedCount++;
	}
	RPLLoader_ReleaseCodeCaveMem(allocation);

	cemuLog_log(LogType::Force, "JIT ARM64 differential: result={} passed={} failed={} total={}",
		passedCount == cases.size() ? "PASS" : "FAIL", passedCount,
		cases.size() - passedCount, cases.size());
}
