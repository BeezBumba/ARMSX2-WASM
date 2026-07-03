#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace armsx2::wasm
{
// ============================================================================
// Flat EE CPU state for the WASM IR pipeline.
//
// This mirrors the essential fields of the real cpuRegisters / fpuRegisters
// structures but is self-contained (no dependency on the full emulator core).
// The IR interpreter and future WASM emitter read/write this struct directly.
// ============================================================================

// A 128-bit general-purpose register (R5900 GPRs are 128 bits wide).
union EEGPReg
{
	std::uint64_t u64[2];
	std::int64_t  s64[2];
	std::uint32_t u32[4];
	std::int32_t  s32[4];
	std::uint16_t u16[8];
	std::int16_t  s16[8];
	std::uint8_t  u8[16];
	std::int8_t   s8[16];
};

static_assert(sizeof(EEGPReg) == 16, "EEGPReg must be 128 bits");

// FPU register (COP1 — single-precision).
union EEFPReg
{
	float f;
	std::uint32_t u32;
	std::int32_t  s32;
};

struct EEState
{
	// -- General-purpose registers (r0..r31) --------------------------------
	// r0 is hardwired to zero; writes are discarded.
	std::array<EEGPReg, 32> gpr = {};

	// -- HI / LO (128 bits each — R5900 extends them) ----------------------
	EEGPReg hi = {};
	EEGPReg lo = {};

	// -- Program counter ---------------------------------------------------
	std::uint32_t pc = 0;

	// -- SA (shift amount register, used by QFSRV) -------------------------
	std::uint32_t sa = 0;

	// -- COP0 registers (32 × 32-bit) --------------------------------------
	std::array<std::uint32_t, 32> cop0 = {};

	// -- COP1 (FPU) --------------------------------------------------------
	std::array<EEFPReg, 32> fpr = {};
	std::array<std::uint32_t, 32> fcr = {};
	EEFPReg fpu_acc = {};   // FPU accumulator
	bool fpu_condition = false;

	// -- Emulation bookkeeping ---------------------------------------------
	std::uint32_t cycle = 0;
	bool in_delay_slot = false;
	bool branch_taken = false;
	std::uint32_t branch_target = 0;

	// Halt flag — set by BREAK / unimplemented traps.
	bool halted = false;

	// -- Helpers -----------------------------------------------------------
	void Reset()
	{
		std::memset(this, 0, sizeof(*this));
	}

	// Read a GPR value.  r0 always returns zero.
	const EEGPReg& ReadGPR(unsigned index) const
	{
		static constexpr EEGPReg s_zero = {};
		return (index == 0) ? s_zero : gpr[index & 31];
	}

	// Write a GPR value.  Writes to r0 are silently discarded.
	void WriteGPR(unsigned index, const EEGPReg& value)
	{
		if (index != 0)
			gpr[index & 31] = value;
	}

	// Convenience: write a sign-extended 32-bit result to a GPR.
	void WriteGPR32(unsigned index, std::int32_t value)
	{
		if (index == 0)
			return;
		EEGPReg& r = gpr[index & 31];
		r.s64[0] = static_cast<std::int64_t>(value);
		r.s64[1] = (value < 0) ? std::int64_t(-1) : std::int64_t(0);
	}

	// Convenience: write a 64-bit result to a GPR (upper 64 bits zeroed).
	void WriteGPR64(unsigned index, std::int64_t value)
	{
		if (index == 0)
			return;
		EEGPReg& r = gpr[index & 31];
		r.s64[0] = value;
		r.s64[1] = (value < 0) ? std::int64_t(-1) : std::int64_t(0);
	}
};
} // namespace armsx2::wasm
