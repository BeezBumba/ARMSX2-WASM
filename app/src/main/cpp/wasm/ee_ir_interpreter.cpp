#include "ee_ir_interpreter.h"
#include "wasm_memory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace armsx2::wasm
{
// ============================================================================
// Memory helpers — simple flat physical address model.
// ============================================================================
namespace
{
std::uint32_t ToPhys(std::uint32_t vaddr)
{
	return vaddr & 0x1FFFFFFF;
}

// Hardware register range: 0x10000000–0x1000FFFF (EE I/O registers)
constexpr std::uint32_t HW_REG_BASE = 0x10000000u;
constexpr std::uint32_t HW_REG_END  = 0x10010000u;

inline bool IsHWReg(std::uint32_t pa)
{
	return pa >= HW_REG_BASE && pa < HW_REG_END;
}
} // anonymous namespace

std::uint8_t EEIRInterpreter::ReadMem8(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	std::uint8_t value = 0;
	if (ReadBootstrapEEMemory(vaddr, &value, sizeof(value)))
		return value;

	const std::uint32_t pa = ToPhys(vaddr);
	return (pa < sz) ? mem[pa] : 0;
}

std::uint16_t EEIRInterpreter::ReadMem16(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	std::uint16_t value = 0;
	if (ReadBootstrapEEMemory(vaddr, reinterpret_cast<u8*>(&value), sizeof(value)))
		return value;

	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 2 > sz) return 0;
	std::memcpy(&value, mem + pa, 2);
	return value;
}

std::uint32_t EEIRInterpreter::ReadMem32(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	std::uint32_t value = 0;
	if (ReadBootstrapEEMemory(vaddr, reinterpret_cast<u8*>(&value), sizeof(value)))
		return value;

	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 4 > sz) return 0;
	std::memcpy(&value, mem + pa, 4);
	return value;
}

std::uint64_t EEIRInterpreter::ReadMem64(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	std::uint64_t value = 0;
	if (ReadBootstrapEEMemory(vaddr, reinterpret_cast<u8*>(&value), sizeof(value)))
		return value;

	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 8 > sz) return 0;
	std::memcpy(&value, mem + pa, 8);
	return value;
}

void EEIRInterpreter::WriteMem8(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint8_t val)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa < sz) mem[pa] = val;
}

void EEIRInterpreter::WriteMem16(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint16_t val)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 2 <= sz) std::memcpy(mem + pa, &val, 2);
}

void EEIRInterpreter::WriteMem32(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint32_t val)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 4 <= sz) std::memcpy(mem + pa, &val, 4);
}

void EEIRInterpreter::WriteMem64(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint64_t val)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 8 <= sz) std::memcpy(mem + pa, &val, 8);
}

// ============================================================================
// Execute — walk every IR node in the block sequentially.
// ============================================================================
EEIRInterpreter::RunResult EEIRInterpreter::Execute(
	const EEIRBlock& block, EEState& state,
	std::uint8_t* ee_memory, std::uint32_t ee_memory_size,
	EEHWRegs* hw) const
{
	RunResult result;
	result.next_pc = block.end_pc; // default: fall through

	// --------------------------------------------------------------------------
	// Hardware-aware memory access lambdas.
	// Reads/writes to 0x10000000–0x1000FFFF are routed to the EE HW register
	// state (INTC, timers, etc.) instead of flat RAM.
	// --------------------------------------------------------------------------
	auto read32 = [&](std::uint32_t vaddr) -> std::uint32_t {
		const std::uint32_t pa = ToPhys(vaddr);
		if (hw && IsHWReg(pa)) return hw->Read32(pa);
		return ReadMem32(ee_memory, ee_memory_size, vaddr);
	};
	auto write32 = [&](std::uint32_t vaddr, std::uint32_t val) {
		const std::uint32_t pa = ToPhys(vaddr);
		if (hw && IsHWReg(pa)) { hw->Write32(pa, val); return; }
		WriteMem32(ee_memory, ee_memory_size, vaddr, val);
	};

	// Delay-slot tracking.  When a branch IR node is encountered we record the
	// target but don't change PC until the block ends.
	bool branch_taken = false;
	std::uint32_t branch_target = 0;

	auto sat32 = [](std::int64_t v) -> std::int32_t
	{
		if (v > std::numeric_limits<std::int32_t>::max()) return std::numeric_limits<std::int32_t>::max();
		if (v < std::numeric_limits<std::int32_t>::min()) return std::numeric_limits<std::int32_t>::min();
		return static_cast<std::int32_t>(v);
	};
	auto satu32 = [](std::int64_t v) -> std::uint32_t
	{
		if (v > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) return std::numeric_limits<std::uint32_t>::max();
		if (v < 0) return 0;
		return static_cast<std::uint32_t>(v);
	};
	auto sat16 = [](std::int32_t v) -> std::int16_t
	{
		if (v > std::numeric_limits<std::int16_t>::max()) return std::numeric_limits<std::int16_t>::max();
		if (v < std::numeric_limits<std::int16_t>::min()) return std::numeric_limits<std::int16_t>::min();
		return static_cast<std::int16_t>(v);
	};
	auto satu16 = [](std::int32_t v) -> std::uint16_t
	{
		if (v > static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())) return std::numeric_limits<std::uint16_t>::max();
		if (v < 0) return 0;
		return static_cast<std::uint16_t>(v);
	};
	auto sat8 = [](std::int16_t v) -> std::int8_t
	{
		if (v > std::numeric_limits<std::int8_t>::max()) return std::numeric_limits<std::int8_t>::max();
		if (v < std::numeric_limits<std::int8_t>::min()) return std::numeric_limits<std::int8_t>::min();
		return static_cast<std::int8_t>(v);
	};
	auto satu8 = [](std::int16_t v) -> std::uint8_t
	{
		if (v > static_cast<std::int16_t>(std::numeric_limits<std::uint8_t>::max())) return std::numeric_limits<std::uint8_t>::max();
		if (v < 0) return 0;
		return static_cast<std::uint8_t>(v);
	};
	auto leading_sign_bits = [](std::int32_t v) -> std::uint32_t
	{
		const std::uint32_t x = static_cast<std::uint32_t>(v ^ (v >> 31));
		return x ? static_cast<std::uint32_t>(__builtin_clz(x) - 1) : 31u;
	};
	auto write_lohi_even_words = [&](std::uint8_t dst)
	{
		if (dst == EEIRReg::INVALID)
			return;
		EEGPReg r = {};
		r.u32[0] = state.lo.u32[0];
		r.u32[1] = state.hi.u32[0];
		r.u32[2] = state.lo.u32[2];
		r.u32[3] = state.hi.u32[2];
		state.WriteGPR(dst, r);
	};

	for (size_t i = 0; i < block.instructions.size(); i++)
	{
		const auto& n = block.instructions[i];
		result.instructions_run++;

		switch (n.op)
		{
		// ---- Meta -----------------------------------------------------------
		case EEIROp::Nop:
		case EEIROp::Cache:
		case EEIROp::Pref:
		case EEIROp::Sync:
			break;

		case EEIROp::Halt:
			result.halted = true;
			return result;

		case EEIROp::BlockEnd:
			result.next_pc = static_cast<std::uint32_t>(n.imm);
			return result;

		case EEIROp::Syscall:
			// Vector to BIOS syscall handler at 0x80000180 (EE exception vector).
			// The actual BIOS HLE will dispatch from there; for now set EPC to
			// the current PC (block.end_pc - 8 = the syscall instruction address).
			state.cop0[14] = block.end_pc - 8;
			state.cop0[13] = (state.cop0[13] & ~0xFFu) | (8u << 2);
			result.next_pc = 0x80000180u;
			return result;

		case EEIROp::Break:
			result.halted = true;
			result.error = "BREAK encountered";
			return result;

		case EEIROp::Eret:
			// Return from exception: PC = EPC (COP0 register 14).
			result.next_pc = state.cop0[14];
			return result;

		// ---- Integer ALU (32-bit) -------------------------------------------
		case EEIROp::Add:
		case EEIROp::AddU:
		{
			const std::int32_t a = state.ReadGPR(n.src0).s32[0];
			const std::int32_t b = state.ReadGPR(n.src1).s32[0];
			state.WriteGPR32(n.dst, a + b);
			break;
		}

		case EEIROp::Sub:
		case EEIROp::SubU:
		{
			const std::int32_t a = state.ReadGPR(n.src0).s32[0];
			const std::int32_t b = state.ReadGPR(n.src1).s32[0];
			state.WriteGPR32(n.dst, a - b);
			break;
		}

		case EEIROp::AddImm:
		case EEIROp::AddImmU:
		{
			const std::int32_t a = state.ReadGPR(n.src0).s32[0];
			const std::int16_t imm = static_cast<std::int16_t>(n.imm);
			state.WriteGPR32(n.dst, a + imm);
			break;
		}

		// ---- Integer ALU (64-bit) -------------------------------------------
		case EEIROp::DAdd:
		case EEIROp::DAddU:
		{
			const std::int64_t a = state.ReadGPR(n.src0).s64[0];
			const std::int64_t b = state.ReadGPR(n.src1).s64[0];
			state.WriteGPR64(n.dst, a + b);
			break;
		}

		case EEIROp::DSub:
		case EEIROp::DSubU:
		{
			const std::int64_t a = state.ReadGPR(n.src0).s64[0];
			const std::int64_t b = state.ReadGPR(n.src1).s64[0];
			state.WriteGPR64(n.dst, a - b);
			break;
		}

		case EEIROp::DAddImm:
		case EEIROp::DAddImmU:
		{
			const std::int64_t a = state.ReadGPR(n.src0).s64[0];
			const std::int16_t imm = static_cast<std::int16_t>(n.imm);
			state.WriteGPR64(n.dst, a + static_cast<std::int64_t>(imm));
			break;
		}

		// ---- Logical --------------------------------------------------------
		case EEIROp::And:
		{
			EEGPReg r;
			r.u64[0] = state.ReadGPR(n.src0).u64[0] & state.ReadGPR(n.src1).u64[0];
			r.u64[1] = state.ReadGPR(n.src0).u64[1] & state.ReadGPR(n.src1).u64[1];
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Or:
		{
			EEGPReg r;
			r.u64[0] = state.ReadGPR(n.src0).u64[0] | state.ReadGPR(n.src1).u64[0];
			r.u64[1] = state.ReadGPR(n.src0).u64[1] | state.ReadGPR(n.src1).u64[1];
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Xor:
		{
			EEGPReg r;
			r.u64[0] = state.ReadGPR(n.src0).u64[0] ^ state.ReadGPR(n.src1).u64[0];
			r.u64[1] = state.ReadGPR(n.src0).u64[1] ^ state.ReadGPR(n.src1).u64[1];
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Nor:
		{
			EEGPReg r;
			r.u64[0] = ~(state.ReadGPR(n.src0).u64[0] | state.ReadGPR(n.src1).u64[0]);
			r.u64[1] = ~(state.ReadGPR(n.src0).u64[1] | state.ReadGPR(n.src1).u64[1]);
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::AndImm:
		{
			const std::uint64_t imm = static_cast<std::uint64_t>(static_cast<std::uint16_t>(n.imm));
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] & imm));
			break;
		}

		case EEIROp::OrImm:
		{
			const std::uint64_t imm = static_cast<std::uint64_t>(static_cast<std::uint16_t>(n.imm));
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] | imm));
			break;
		}

		case EEIROp::XorImm:
		{
			const std::uint64_t imm = static_cast<std::uint64_t>(static_cast<std::uint16_t>(n.imm));
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] ^ imm));
			break;
		}

		// ---- Set on Less Than -----------------------------------------------
		case EEIROp::Slt:
			state.WriteGPR64(n.dst, (state.ReadGPR(n.src0).s64[0] < state.ReadGPR(n.src1).s64[0]) ? 1 : 0);
			break;

		case EEIROp::SltU:
			state.WriteGPR64(n.dst, (state.ReadGPR(n.src0).u64[0] < state.ReadGPR(n.src1).u64[0]) ? 1 : 0);
			break;

		case EEIROp::SltImm:
		{
			const std::int64_t imm = static_cast<std::int64_t>(static_cast<std::int16_t>(n.imm));
			state.WriteGPR64(n.dst, (state.ReadGPR(n.src0).s64[0] < imm) ? 1 : 0);
			break;
		}

		case EEIROp::SltImmU:
		{
			const std::uint64_t imm = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(n.imm)));
			state.WriteGPR64(n.dst, (state.ReadGPR(n.src0).u64[0] < imm) ? 1 : 0);
			break;
		}

		// ---- Shifts (32-bit) ------------------------------------------------
		case EEIROp::Sll:
		{
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 31;
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(state.ReadGPR(n.src0).u32[0] << sa));
			break;
		}

		case EEIROp::Srl:
		{
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 31;
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(state.ReadGPR(n.src0).u32[0] >> sa));
			break;
		}

		case EEIROp::Sra:
		{
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 31;
			state.WriteGPR32(n.dst, state.ReadGPR(n.src0).s32[0] >> sa);
			break;
		}

		case EEIROp::SllV:
		{
			const std::uint32_t sa = state.ReadGPR(n.src1).u32[0] & 31;
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(state.ReadGPR(n.src0).u32[0] << sa));
			break;
		}

		case EEIROp::SrlV:
		{
			const std::uint32_t sa = state.ReadGPR(n.src1).u32[0] & 31;
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(state.ReadGPR(n.src0).u32[0] >> sa));
			break;
		}

		case EEIROp::SraV:
		{
			const std::uint32_t sa = state.ReadGPR(n.src1).u32[0] & 31;
			state.WriteGPR32(n.dst, state.ReadGPR(n.src0).s32[0] >> sa);
			break;
		}

		// ---- Shifts (64-bit) ------------------------------------------------
		case EEIROp::DSll:
		{
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 63;
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] << sa));
			break;
		}

		case EEIROp::DSrl:
		{
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 63;
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] >> sa));
			break;
		}

		case EEIROp::DSra:
		{
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 63;
			state.WriteGPR64(n.dst, state.ReadGPR(n.src0).s64[0] >> sa);
			break;
		}

		case EEIROp::DSll32:
		{
			const std::uint32_t sa = (static_cast<std::uint32_t>(n.imm) & 31) + 32;
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] << sa));
			break;
		}

		case EEIROp::DSrl32:
		{
			const std::uint32_t sa = (static_cast<std::uint32_t>(n.imm) & 31) + 32;
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] >> sa));
			break;
		}

		case EEIROp::DSra32:
		{
			const std::uint32_t sa = (static_cast<std::uint32_t>(n.imm) & 31) + 32;
			state.WriteGPR64(n.dst, state.ReadGPR(n.src0).s64[0] >> sa);
			break;
		}

		case EEIROp::DSllV:
		{
			const std::uint32_t sa = state.ReadGPR(n.src1).u32[0] & 63;
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] << sa));
			break;
		}

		case EEIROp::DSrlV:
		{
			const std::uint32_t sa = state.ReadGPR(n.src1).u32[0] & 63;
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.ReadGPR(n.src0).u64[0] >> sa));
			break;
		}

		case EEIROp::DSraV:
		{
			const std::uint32_t sa = state.ReadGPR(n.src1).u32[0] & 63;
			state.WriteGPR64(n.dst, state.ReadGPR(n.src0).s64[0] >> sa);
			break;
		}

		// ---- Multiply / Divide ----------------------------------------------
		case EEIROp::Mult:
		{
			const std::int64_t product = static_cast<std::int64_t>(state.ReadGPR(n.src0).s32[0]) *
										 static_cast<std::int64_t>(state.ReadGPR(n.src1).s32[0]);
			state.lo.s64[0] = static_cast<std::int64_t>(static_cast<std::int32_t>(product));
			state.hi.s64[0] = static_cast<std::int64_t>(static_cast<std::int32_t>(product >> 32));
			if (n.dst != EEIRReg::INVALID)
				state.WriteGPR64(n.dst, state.lo.s64[0]);
			break;
		}

		case EEIROp::MultU:
		{
			const std::uint64_t product = static_cast<std::uint64_t>(state.ReadGPR(n.src0).u32[0]) *
										  static_cast<std::uint64_t>(state.ReadGPR(n.src1).u32[0]);
			state.lo.s64[0] = static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(product)));
			state.hi.s64[0] = static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(product >> 32)));
			if (n.dst != EEIRReg::INVALID)
				state.WriteGPR64(n.dst, state.lo.s64[0]);
			break;
		}

		case EEIROp::Div:
		{
			const std::int32_t a = state.ReadGPR(n.src0).s32[0];
			const std::int32_t b = state.ReadGPR(n.src1).s32[0];
			if (b != 0)
			{
				state.lo.s64[0] = static_cast<std::int64_t>(a / b);
				state.hi.s64[0] = static_cast<std::int64_t>(a % b);
			}
			break;
		}

		case EEIROp::DivU:
		{
			const std::uint32_t a = state.ReadGPR(n.src0).u32[0];
			const std::uint32_t b = state.ReadGPR(n.src1).u32[0];
			if (b != 0)
			{
				state.lo.s64[0] = static_cast<std::int64_t>(static_cast<std::int32_t>(a / b));
				state.hi.s64[0] = static_cast<std::int64_t>(static_cast<std::int32_t>(a % b));
			}
			break;
		}

		// ---- LUI ------------------------------------------------------------
		case EEIROp::Lui:
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(static_cast<std::int16_t>(n.imm)) << 16);
			break;

		// ---- HI/LO moves ----------------------------------------------------
		case EEIROp::Mfhi:
		{
			state.WriteGPR(n.dst, state.hi);
			break;
		}

		case EEIROp::Mthi:
			state.hi = state.ReadGPR(n.src0);
			break;

		case EEIROp::Mflo:
			state.WriteGPR(n.dst, state.lo);
			break;

		case EEIROp::Mtlo:
			state.lo = state.ReadGPR(n.src0);
			break;

		// ---- SA moves -------------------------------------------------------
		case EEIROp::Mfsa:
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(state.sa));
			break;

		case EEIROp::Mtsa:
			state.sa = state.ReadGPR(n.src0).u32[0];
			break;

		case EEIROp::Mtsab:
		{
			const std::uint32_t rs = state.ReadGPR(n.src0).u32[0];
			const std::uint32_t imm = static_cast<std::uint32_t>(n.imm) & 0xF;
			state.sa = ((rs & 0xF) ^ imm) << 3;
			break;
		}

		case EEIROp::Mtsah:
		{
			const std::uint32_t rs = state.ReadGPR(n.src0).u32[0];
			const std::uint32_t imm = static_cast<std::uint32_t>(n.imm) & 0x7;
			state.sa = ((rs & 0x7) ^ imm) << 4;
			break;
		}

		// ---- Conditional moves ----------------------------------------------
		case EEIROp::MovZ:
			if (state.ReadGPR(n.src1).u64[0] == 0)
				state.WriteGPR(n.dst, state.ReadGPR(n.src0));
			break;

		case EEIROp::MovN:
			if (state.ReadGPR(n.src1).u64[0] != 0)
				state.WriteGPR(n.dst, state.ReadGPR(n.src0));
			break;

		// ---- Loads ----------------------------------------------------------
		case EEIROp::LB:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR32(n.dst, static_cast<std::int8_t>(ReadMem8(ee_memory, ee_memory_size, addr)));
			break;
		}

		case EEIROp::LBU:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(ReadMem8(ee_memory, ee_memory_size, addr)));
			break;
		}

		case EEIROp::LH:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR32(n.dst, static_cast<std::int16_t>(ReadMem16(ee_memory, ee_memory_size, addr)));
			break;
		}

		case EEIROp::LHU:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(ReadMem16(ee_memory, ee_memory_size, addr)));
			break;
		}

		case EEIROp::LW:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(read32(addr)));
			break;
		}

		case EEIROp::LWU:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(static_cast<std::uint32_t>(read32(addr))));
			break;
		}

		case EEIROp::LD:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(ReadMem64(ee_memory, ee_memory_size, addr)));
			break;
		}

		case EEIROp::LQ:
		{
			const std::uint32_t addr = (state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm)) & ~0xFu;
			EEGPReg r;
			r.u64[0] = ReadMem64(ee_memory, ee_memory_size, addr);
			r.u64[1] = ReadMem64(ee_memory, ee_memory_size, addr + 8);
			state.WriteGPR(n.dst, r);
			break;
		}

		// Unaligned loads — simplified implementation.
		case EEIROp::LWL:
		case EEIROp::LWR:
		case EEIROp::LDL:
		case EEIROp::LDR:
		{
			// Simplified: just do an aligned load for now.
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			if (n.op == EEIROp::LWL || n.op == EEIROp::LWR)
				state.WriteGPR32(n.dst, static_cast<std::int32_t>(read32(addr & ~3u)));
			else
				state.WriteGPR64(n.dst, static_cast<std::int64_t>(ReadMem64(ee_memory, ee_memory_size, addr & ~7u)));
			break;
		}

		// ---- Stores ---------------------------------------------------------
		case EEIROp::SB:
		{
			// For stores: dst = base (addr reg), src0 = value reg
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			WriteMem8(ee_memory, ee_memory_size, addr, state.ReadGPR(n.src0).u8[0]);
			break;
		}

		case EEIROp::SH:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			WriteMem16(ee_memory, ee_memory_size, addr, state.ReadGPR(n.src0).u16[0]);
			break;
		}

		case EEIROp::SW:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			write32(addr, state.ReadGPR(n.src0).u32[0]);
			break;
		}

		case EEIROp::SD:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			WriteMem64(ee_memory, ee_memory_size, addr, state.ReadGPR(n.src0).u64[0]);
			break;
		}

		case EEIROp::SQ:
		{
			const std::uint32_t addr = (state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm)) & ~0xFu;
			WriteMem64(ee_memory, ee_memory_size, addr, state.ReadGPR(n.src0).u64[0]);
			WriteMem64(ee_memory, ee_memory_size, addr + 8, state.ReadGPR(n.src0).u64[1]);
			break;
		}

		// Unaligned stores — simplified.
		case EEIROp::SWL:
		case EEIROp::SWR:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			write32(addr & ~3u, state.ReadGPR(n.src0).u32[0]);
			break;
		}

		case EEIROp::SDL:
		case EEIROp::SDR:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			WriteMem64(ee_memory, ee_memory_size, addr & ~7u, state.ReadGPR(n.src0).u64[0]);
			break;
		}

		// ---- Branches -------------------------------------------------------
		// Branch evaluation: check condition, record target.  The delay-slot
		// instruction has already been included in the block by the lifter.
		case EEIROp::Beq:
		case EEIROp::BeqL:
		{
			if (state.ReadGPR(n.src0).u64[0] == state.ReadGPR(n.src1).u64[0])
			{
				branch_taken = true;
				// The branch is the second-to-last instruction in the block
				// (last is the delay slot).  block.end_pc points past the delay
				// slot, so (end_pc - 4) is the delay-slot PC, which is also the
				// PC of the instruction after the branch — the standard MIPS
				// reference point for branch offsets.
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Bne:
		case EEIROp::BneL:
		{
			if (state.ReadGPR(n.src0).u64[0] != state.ReadGPR(n.src1).u64[0])
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Blez:
		case EEIROp::BlezL:
		{
			if (state.ReadGPR(n.src0).s64[0] <= 0)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Bgtz:
		case EEIROp::BgtzL:
		{
			if (state.ReadGPR(n.src0).s64[0] > 0)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Bltz:
		case EEIROp::BltzL:
		{
			if (state.ReadGPR(n.src0).s64[0] < 0)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Bgez:
		case EEIROp::BgezL:
		{
			if (state.ReadGPR(n.src0).s64[0] >= 0)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Bgezal:
		case EEIROp::BgezalL:
		{
			// Link: store return address regardless of branch outcome.
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(block.end_pc));
			if (state.ReadGPR(n.src0).s64[0] >= 0)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::Bltzal:
		case EEIROp::BltzalL:
		{
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(block.end_pc));
			if (state.ReadGPR(n.src0).s64[0] < 0)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		// ---- Jumps ----------------------------------------------------------
		case EEIROp::J:
		{
			branch_taken = true;
			// Target = (PC & 0xF000'0000) | (imm26 << 2)
			branch_target = (block.start_pc & 0xF0000000) | (static_cast<std::uint32_t>(n.imm) << 2);
			break;
		}

		case EEIROp::Jal:
		{
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(block.end_pc));
			branch_taken = true;
			branch_target = (block.start_pc & 0xF0000000) | (static_cast<std::uint32_t>(n.imm) << 2);
			break;
		}

		case EEIROp::Jr:
		{
			branch_taken = true;
			branch_target = state.ReadGPR(n.src0).u32[0];
			break;
		}

		case EEIROp::Jalr:
		{
			branch_target = state.ReadGPR(n.src0).u32[0];
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(block.end_pc));
			branch_taken = true;
			break;
		}

		// ---- COP0 -----------------------------------------------------------
		case EEIROp::Mfc0:
		{
			// src0 is COP0 register index (128 + i), extract the raw index.
			const unsigned cop0_idx = n.src0 - 128;
			if (cop0_idx < 32)
				state.WriteGPR32(n.dst, static_cast<std::int32_t>(state.cop0[cop0_idx]));
			break;
		}

		case EEIROp::Mtc0:
		{
			const unsigned cop0_idx = n.dst - 128;
			if (cop0_idx < 32)
				state.cop0[cop0_idx] = state.ReadGPR(n.src0).u32[0];
			break;
		}

		case EEIROp::Tlbr:
		case EEIROp::Tlbwi:
		case EEIROp::Tlbwr:
		case EEIROp::Tlbp:
		case EEIROp::Di:
		case EEIROp::Ei:
			// Stubs — real TLB/interrupt handling comes later.
			break;

		// ---- COP1 (FPU) -----------------------------------------------------
		case EEIROp::Mfc1:
		{
			const unsigned fpr_idx = n.src0 - 64;
			if (fpr_idx < 32)
				state.WriteGPR32(n.dst, state.fpr[fpr_idx].s32);
			break;
		}

		case EEIROp::Mtc1:
		{
			const unsigned fpr_idx = n.dst - 64;
			if (fpr_idx < 32)
				state.fpr[fpr_idx].u32 = state.ReadGPR(n.src0).u32[0];
			break;
		}

		case EEIROp::Cfc1:
		{
			const unsigned fcr_idx = static_cast<unsigned>(n.imm) & 31;
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(state.fcr[fcr_idx]));
			break;
		}

		case EEIROp::Ctc1:
		{
			const unsigned fcr_idx = static_cast<unsigned>(n.imm) & 31;
			state.fcr[fcr_idx] = state.ReadGPR(n.src0).u32[0];
			break;
		}

		case EEIROp::FAdd:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
				state.fpr[fd].f = state.fpr[fs].f + state.fpr[ft].f;
			break;
		}

		case EEIROp::FSub:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
				state.fpr[fd].f = state.fpr[fs].f - state.fpr[ft].f;
			break;
		}

		case EEIROp::FMul:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
				state.fpr[fd].f = state.fpr[fs].f * state.fpr[ft].f;
			break;
		}

		case EEIROp::FDiv:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32 && state.fpr[ft].f != 0.0f)
				state.fpr[fd].f = state.fpr[fs].f / state.fpr[ft].f;
			break;
		}

		case EEIROp::FSqrt:
		{
			const unsigned fd = n.dst - 64, ft = n.src0 - 64;
			if (fd < 32 && ft < 32)
				state.fpr[fd].f = std::sqrt(std::abs(state.fpr[ft].f));
			break;
		}

		case EEIROp::FAbs:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64;
			if (fd < 32 && fs < 32)
				state.fpr[fd].f = std::abs(state.fpr[fs].f);
			break;
		}

		case EEIROp::FNeg:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64;
			if (fd < 32 && fs < 32)
				state.fpr[fd].f = -state.fpr[fs].f;
			break;
		}

		case EEIROp::FMov:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64;
			if (fd < 32 && fs < 32)
				state.fpr[fd] = state.fpr[fs];
			break;
		}

		case EEIROp::CvtSW:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64;
			if (fd < 32 && fs < 32)
				state.fpr[fd].f = static_cast<float>(state.fpr[fs].s32);
			break;
		}

		case EEIROp::CvtWS:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64;
			if (fd < 32 && fs < 32)
				state.fpr[fd].s32 = static_cast<std::int32_t>(state.fpr[fs].f);
			break;
		}

		case EEIROp::FCmpEQ:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_condition = (state.fpr[fs].f == state.fpr[ft].f);
			break;
		}

		case EEIROp::FCmpLT:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_condition = (state.fpr[fs].f < state.fpr[ft].f);
			break;
		}

		case EEIROp::FCmpLE:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_condition = (state.fpr[fs].f <= state.fpr[ft].f);
			break;
		}

		case EEIROp::BC1F:
		case EEIROp::BC1FL:
		{
			if (!state.fpu_condition)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::BC1T:
		case EEIROp::BC1TL:
		{
			if (state.fpu_condition)
			{
				branch_taken = true;
				branch_target = static_cast<std::uint32_t>(
					static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
			}
			break;
		}

		case EEIROp::LWC1:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			const unsigned fpr_idx = n.dst - 64;
			if (fpr_idx < 32)
				state.fpr[fpr_idx].u32 = read32(addr);
			break;
		}

		case EEIROp::SWC1:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			const unsigned fpr_idx = n.src0 - 64;
			if (fpr_idx < 32)
				write32(addr, state.fpr[fpr_idx].u32);
			break;
		}

		// FPU accumulator ops
		case EEIROp::FAdda:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_acc.f = state.fpr[fs].f + state.fpr[ft].f;
			break;
		}

		case EEIROp::FSuba:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_acc.f = state.fpr[fs].f - state.fpr[ft].f;
			break;
		}

		case EEIROp::FMula:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_acc.f = state.fpr[fs].f * state.fpr[ft].f;
			break;
		}

		case EEIROp::FMadd:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
			{
				state.fpu_acc.f += state.fpr[fs].f * state.fpr[ft].f;
				state.fpr[fd].f = state.fpu_acc.f;
			}
			break;
		}

		case EEIROp::FMsub:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
			{
				state.fpu_acc.f -= state.fpr[fs].f * state.fpr[ft].f;
				state.fpr[fd].f = state.fpu_acc.f;
			}
			break;
		}

		case EEIROp::FMadda:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_acc.f += state.fpr[fs].f * state.fpr[ft].f;
			break;
		}

		case EEIROp::FMsuba:
		{
			const unsigned fs = n.src0 - 64, ft = n.src1 - 64;
			if (fs < 32 && ft < 32)
				state.fpu_acc.f -= state.fpr[fs].f * state.fpr[ft].f;
			break;
		}

		case EEIROp::FMax:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
				state.fpr[fd].f = std::max(state.fpr[fs].f, state.fpr[ft].f);
			break;
		}

		case EEIROp::FMin:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
				state.fpr[fd].f = std::min(state.fpr[fs].f, state.fpr[ft].f);
			break;
		}

		case EEIROp::FRsqrt:
		{
			const unsigned fd = n.dst - 64, fs = n.src0 - 64, ft = n.src1 - 64;
			if (fd < 32 && fs < 32 && ft < 32)
			{
				const float sq = std::sqrt(std::abs(state.fpr[ft].f));
				state.fpr[fd].f = (sq != 0.0f) ? (state.fpr[fs].f / sq) : 0.0f;
			}
			break;
		}

		// ---- Traps (simplified — halt on trap) ------------------------------
		case EEIROp::Tge:
		case EEIROp::TgeU:
		case EEIROp::Tlt:
		case EEIROp::TltU:
		case EEIROp::Teq:
		case EEIROp::Tne:
		case EEIROp::TgeImm:
		case EEIROp::TgeImmU:
		case EEIROp::TltImm:
		case EEIROp::TltImmU:
		case EEIROp::TeqImm:
		case EEIROp::TneImm:
			// For now, ignore traps (most games don't trigger them).
			break;

		// ---- COP2 / LQC2 / SQC2 placeholder --------------------------------
		case EEIROp::COP2:
		case EEIROp::LQC2:
		case EEIROp::SQC2:
			// Stub — VU0 macro mode will be added later.
			break;

		// ---- MMI pipeline 1 HI1/LO1 ----------------------------------------
		case EEIROp::Mfhi1:
			state.WriteGPR64(n.dst, state.hi.s64[1]);
			break;

		case EEIROp::Mthi1:
			state.hi.u64[1] = state.ReadGPR(n.src0).u64[0];
			break;

		case EEIROp::Mflo1:
			state.WriteGPR64(n.dst, state.lo.s64[1]);
			break;

		case EEIROp::Mtlo1:
			state.lo.u64[1] = state.ReadGPR(n.src0).u64[0];
			break;

		// ---- MMI multiply/divide and accumulate ----------------------------
		case EEIROp::Mult1:
		{
			const std::int64_t product = static_cast<std::int64_t>(state.ReadGPR(n.src0).s32[0]) *
				static_cast<std::int64_t>(state.ReadGPR(n.src1).s32[0]);
			state.lo.s64[1] = static_cast<std::int32_t>(product);
			state.hi.s64[1] = static_cast<std::int32_t>(product >> 32);
			if (n.dst != EEIRReg::INVALID)
				state.WriteGPR64(n.dst, state.lo.s64[1]);
			break;
		}

		case EEIROp::MultU1:
		{
			const std::uint64_t product = static_cast<std::uint64_t>(state.ReadGPR(n.src0).u32[0]) *
				static_cast<std::uint64_t>(state.ReadGPR(n.src1).u32[0]);
			state.lo.s64[1] = static_cast<std::int32_t>(static_cast<std::uint32_t>(product));
			state.hi.s64[1] = static_cast<std::int32_t>(static_cast<std::uint32_t>(product >> 32));
			if (n.dst != EEIRReg::INVALID)
				state.WriteGPR64(n.dst, state.lo.s64[1]);
			break;
		}

		case EEIROp::Div1:
		{
			const std::int32_t a = state.ReadGPR(n.src0).s32[0];
			const std::int32_t b = state.ReadGPR(n.src1).s32[0];
			if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
			{
				state.lo.s64[1] = std::numeric_limits<std::int32_t>::min();
				state.hi.s64[1] = 0;
			}
			else if (b != 0)
			{
				state.lo.s64[1] = a / b;
				state.hi.s64[1] = a % b;
			}
			else
			{
				state.lo.s64[1] = (a < 0) ? 1 : -1;
				state.hi.s64[1] = a;
			}
			break;
		}

		case EEIROp::DivU1:
		{
			const std::uint32_t a = state.ReadGPR(n.src0).u32[0];
			const std::uint32_t b = state.ReadGPR(n.src1).u32[0];
			if (b != 0)
			{
				state.lo.s64[1] = static_cast<std::int32_t>(a / b);
				state.hi.s64[1] = static_cast<std::int32_t>(a % b);
			}
			else
			{
				state.lo.s64[1] = -1;
				state.hi.s64[1] = state.ReadGPR(n.src0).s32[0];
			}
			break;
		}

		case EEIROp::Madd:
		case EEIROp::MaddU:
		case EEIROp::Madd1:
		case EEIROp::MaddU1:
		{
			const bool pipe1 = (n.op == EEIROp::Madd1 || n.op == EEIROp::MaddU1);
			const bool is_unsigned = (n.op == EEIROp::MaddU || n.op == EEIROp::MaddU1);
			const int lane = pipe1 ? 1 : 0;
			const std::uint32_t lo_idx = static_cast<std::uint32_t>(lane * 2);
			const std::uint32_t hi_idx = static_cast<std::uint32_t>(lane * 2);
			if (is_unsigned)
			{
				std::uint64_t acc = static_cast<std::uint64_t>(state.lo.u32[lo_idx]) |
					(static_cast<std::uint64_t>(state.hi.u32[hi_idx]) << 32);
				acc += static_cast<std::uint64_t>(state.ReadGPR(n.src0).u32[0]) *
					static_cast<std::uint64_t>(state.ReadGPR(n.src1).u32[0]);
				state.lo.s64[lane] = static_cast<std::int32_t>(static_cast<std::uint32_t>(acc));
				state.hi.s64[lane] = static_cast<std::int32_t>(static_cast<std::uint32_t>(acc >> 32));
			}
			else
			{
				std::int64_t acc = static_cast<std::uint64_t>(state.lo.u32[lo_idx]) |
					(static_cast<std::uint64_t>(state.hi.u32[hi_idx]) << 32);
				acc += static_cast<std::int64_t>(state.ReadGPR(n.src0).s32[0]) *
					static_cast<std::int64_t>(state.ReadGPR(n.src1).s32[0]);
				state.lo.s64[lane] = static_cast<std::int32_t>(acc);
				state.hi.s64[lane] = static_cast<std::int32_t>(acc >> 32);
			}
			if (n.dst != EEIRReg::INVALID)
				state.WriteGPR64(n.dst, state.lo.s64[lane]);
			break;
		}

		// ---- MMI simple scalar/vector ops -----------------------------------
		case EEIROp::Plzcw:
		{
			EEGPReg r = {};
			const EEGPReg& src = state.ReadGPR(n.src0);
			r.u32[0] = leading_sign_bits(src.s32[0]);
			r.u32[1] = leading_sign_bits(src.s32[1]);
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pmfhl:
		{
			EEGPReg r = {};
			switch (static_cast<std::uint32_t>(n.imm) & 0x7)
			{
				case 0x00:
					r.u32[0] = state.lo.u32[0];
					r.u32[1] = state.hi.u32[0];
					r.u32[2] = state.lo.u32[2];
					r.u32[3] = state.hi.u32[2];
					break;
				case 0x01:
					r.u32[0] = state.lo.u32[1];
					r.u32[1] = state.hi.u32[1];
					r.u32[2] = state.lo.u32[3];
					r.u32[3] = state.hi.u32[3];
					break;
				case 0x02:
					for (int lane = 0; lane < 2; ++lane)
					{
						const std::uint32_t base = static_cast<std::uint32_t>(lane * 2);
						const std::int64_t value = static_cast<std::uint64_t>(state.lo.u32[base]) |
							(static_cast<std::uint64_t>(state.hi.u32[base]) << 32);
						r.s64[lane] = sat32(value);
					}
					break;
				case 0x03:
					r.u16[0] = state.lo.u16[0]; r.u16[1] = state.lo.u16[2];
					r.u16[2] = state.hi.u16[0]; r.u16[3] = state.hi.u16[2];
					r.u16[4] = state.lo.u16[4]; r.u16[5] = state.lo.u16[6];
					r.u16[6] = state.hi.u16[4]; r.u16[7] = state.hi.u16[6];
					break;
				case 0x04:
					for (int lane = 0; lane < 8; ++lane)
					{
						const std::uint32_t src = (lane < 4) ? ((lane < 2) ? state.lo.u32[lane] : state.hi.u32[lane - 2])
							: ((lane < 6) ? state.lo.u32[lane - 2] : state.hi.u32[lane - 4]);
						r.s16[lane] = sat16(static_cast<std::int32_t>(src));
					}
					break;
				default:
					break;
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pmthl:
		{
			const EEGPReg& src = state.ReadGPR(n.src0);
			state.lo.u32[0] = src.u32[0];
			state.hi.u32[0] = src.u32[1];
			state.lo.u32[2] = src.u32[2];
			state.hi.u32[2] = src.u32[3];
			break;
		}

		case EEIROp::Psllh:
		case EEIROp::Psrlh:
		case EEIROp::Psrah:
		{
			EEGPReg r = {};
			const EEGPReg& src = state.ReadGPR(n.src0);
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 0xF;
			for (int lane = 0; lane < 8; ++lane)
			{
				switch (n.op)
				{
					case EEIROp::Psllh: r.u16[lane] = static_cast<std::uint16_t>(src.u16[lane] << sa); break;
					case EEIROp::Psrlh: r.u16[lane] = static_cast<std::uint16_t>(src.u16[lane] >> sa); break;
					default: r.s16[lane] = static_cast<std::int16_t>(src.s16[lane] >> sa); break;
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Psllw:
		case EEIROp::Psrlw:
		case EEIROp::Psraw:
		{
			EEGPReg r = {};
			const EEGPReg& src = state.ReadGPR(n.src0);
			const std::uint32_t sa = static_cast<std::uint32_t>(n.imm) & 0x1F;
			for (int lane = 0; lane < 4; ++lane)
			{
				switch (n.op)
				{
					case EEIROp::Psllw: r.u32[lane] = src.u32[lane] << sa; break;
					case EEIROp::Psrlw: r.u32[lane] = src.u32[lane] >> sa; break;
					default: r.s32[lane] = src.s32[lane] >> sa; break;
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Psllvw:
		case EEIROp::Psrlvw:
		case EEIROp::Psravw:
		{
			EEGPReg r = {};
			const EEGPReg& value = state.ReadGPR(n.src0);
			const EEGPReg& shift = state.ReadGPR(n.src1);
			for (int lane = 0; lane < 4; ++lane)
			{
				const std::uint32_t sa = shift.u32[lane] & 31;
				switch (n.op)
				{
					case EEIROp::Psllvw: r.u32[lane] = value.u32[lane] << sa; break;
					case EEIROp::Psrlvw: r.u32[lane] = value.u32[lane] >> sa; break;
					default: r.s32[lane] = value.s32[lane] >> sa; break;
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Paddw:
		case EEIROp::Psubw:
		case EEIROp::Pcgtw:
		case EEIROp::Pceqw:
		case EEIROp::Pmaxw:
		case EEIROp::Pminw:
		case EEIROp::Pabsw:
		case EEIROp::Padduw:
		case EEIROp::Psubuw:
		{
			EEGPReg r = {};
			const EEGPReg& a = state.ReadGPR(n.src0);
			const EEGPReg& b = state.ReadGPR(n.src1);
			if (n.op == EEIROp::Pabsw)
			{
				const EEGPReg& src = state.ReadGPR(n.src0);
				for (int lane = 0; lane < 4; ++lane)
					r.u32[lane] = (src.u32[lane] == 0x80000000u) ? 0x7FFFFFFFu : static_cast<std::uint32_t>(std::abs(src.s32[lane]));
			}
			else
			{
				for (int lane = 0; lane < 4; ++lane)
				{
					switch (n.op)
					{
						case EEIROp::Paddw:  r.s32[lane] = static_cast<std::int32_t>(a.u32[lane] + b.u32[lane]); break;
						case EEIROp::Psubw:  r.s32[lane] = static_cast<std::int32_t>(a.u32[lane] - b.u32[lane]); break;
						case EEIROp::Pcgtw:  r.u32[lane] = (a.s32[lane] > b.s32[lane]) ? 0xFFFFFFFFu : 0u; break;
						case EEIROp::Pceqw:  r.u32[lane] = (a.s32[lane] == b.s32[lane]) ? 0xFFFFFFFFu : 0u; break;
						case EEIROp::Pmaxw:  r.s32[lane] = std::max(a.s32[lane], b.s32[lane]); break;
						case EEIROp::Pminw:  r.s32[lane] = std::min(a.s32[lane], b.s32[lane]); break;
						case EEIROp::Padduw: r.u32[lane] = satu32(static_cast<std::int64_t>(a.u32[lane]) + static_cast<std::int64_t>(b.u32[lane])); break;
						default:            r.u32[lane] = satu32(static_cast<std::int64_t>(a.u32[lane]) - static_cast<std::int64_t>(b.u32[lane])); break;
					}
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Paddh:
		case EEIROp::Psubh:
		case EEIROp::Pcgth:
		case EEIROp::Pceqh:
		case EEIROp::Pmaxh:
		case EEIROp::Pminh:
		case EEIROp::Pabsh:
		case EEIROp::Padduh:
		case EEIROp::Psubuh:
		case EEIROp::Padsbh:
		{
			EEGPReg r = {};
			const EEGPReg& a = state.ReadGPR(n.src0);
			const EEGPReg& b = state.ReadGPR(n.src1);
			if (n.op == EEIROp::Pabsh)
			{
				const EEGPReg& src = state.ReadGPR(n.src0);
				for (int lane = 0; lane < 8; ++lane)
					r.u16[lane] = (src.u16[lane] == 0x8000u) ? 0x7FFFu : static_cast<std::uint16_t>(std::abs(src.s16[lane]));
			}
			else if (n.op == EEIROp::Padsbh)
			{
				for (int lane = 0; lane < 4; ++lane)
					r.s16[lane] = static_cast<std::int16_t>(a.u16[lane] - b.u16[lane]);
				for (int lane = 4; lane < 8; ++lane)
					r.s16[lane] = static_cast<std::int16_t>(a.u16[lane] + b.u16[lane]);
			}
			else
			{
				for (int lane = 0; lane < 8; ++lane)
				{
					switch (n.op)
					{
						case EEIROp::Paddh:  r.s16[lane] = static_cast<std::int16_t>(a.u16[lane] + b.u16[lane]); break;
						case EEIROp::Psubh:  r.s16[lane] = static_cast<std::int16_t>(a.u16[lane] - b.u16[lane]); break;
						case EEIROp::Pcgth:  r.u16[lane] = (a.s16[lane] > b.s16[lane]) ? 0xFFFFu : 0u; break;
						case EEIROp::Pceqh:  r.u16[lane] = (a.s16[lane] == b.s16[lane]) ? 0xFFFFu : 0u; break;
						case EEIROp::Pmaxh:  r.s16[lane] = std::max(a.s16[lane], b.s16[lane]); break;
						case EEIROp::Pminh:  r.s16[lane] = std::min(a.s16[lane], b.s16[lane]); break;
						case EEIROp::Padduh: r.u16[lane] = satu16(static_cast<std::int32_t>(a.u16[lane]) + static_cast<std::int32_t>(b.u16[lane])); break;
						default:            r.u16[lane] = satu16(static_cast<std::int32_t>(a.u16[lane]) - static_cast<std::int32_t>(b.u16[lane])); break;
					}
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Paddb:
		case EEIROp::Psubb:
		case EEIROp::Pcgtb:
		case EEIROp::Pceqb:
		case EEIROp::Paddub:
		case EEIROp::Psubub:
		{
			EEGPReg r = {};
			const EEGPReg& a = state.ReadGPR(n.src0);
			const EEGPReg& b = state.ReadGPR(n.src1);
			for (int lane = 0; lane < 16; ++lane)
			{
				switch (n.op)
				{
					case EEIROp::Paddb:  r.s8[lane] = static_cast<std::int8_t>(a.u8[lane] + b.u8[lane]); break;
					case EEIROp::Psubb:  r.s8[lane] = static_cast<std::int8_t>(a.u8[lane] - b.u8[lane]); break;
					case EEIROp::Pcgtb:  r.u8[lane] = (a.s8[lane] > b.s8[lane]) ? 0xFFu : 0u; break;
					case EEIROp::Pceqb:  r.u8[lane] = (a.s8[lane] == b.s8[lane]) ? 0xFFu : 0u; break;
					case EEIROp::Paddub: r.u8[lane] = satu8(static_cast<std::int16_t>(a.u8[lane]) + static_cast<std::int16_t>(b.u8[lane])); break;
					default:            r.u8[lane] = satu8(static_cast<std::int16_t>(a.u8[lane]) - static_cast<std::int16_t>(b.u8[lane])); break;
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Paddsw:
		case EEIROp::Psubsw:
		{
			EEGPReg r = {};
			const EEGPReg& a = state.ReadGPR(n.src0);
			const EEGPReg& b = state.ReadGPR(n.src1);
			for (int lane = 0; lane < 4; ++lane)
				r.s32[lane] = (n.op == EEIROp::Paddsw) ? sat32(static_cast<std::int64_t>(a.s32[lane]) + b.s32[lane])
					: sat32(static_cast<std::int64_t>(a.s32[lane]) - b.s32[lane]);
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Paddsh:
		case EEIROp::Psubsh:
		{
			EEGPReg r = {};
			const EEGPReg& a = state.ReadGPR(n.src0);
			const EEGPReg& b = state.ReadGPR(n.src1);
			for (int lane = 0; lane < 8; ++lane)
				r.s16[lane] = (n.op == EEIROp::Paddsh) ? sat16(static_cast<std::int32_t>(a.s16[lane]) + b.s16[lane])
					: sat16(static_cast<std::int32_t>(a.s16[lane]) - b.s16[lane]);
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Paddsb:
		case EEIROp::Psubsb:
		{
			EEGPReg r = {};
			const EEGPReg& a = state.ReadGPR(n.src0);
			const EEGPReg& b = state.ReadGPR(n.src1);
			for (int lane = 0; lane < 16; ++lane)
				r.s8[lane] = (n.op == EEIROp::Paddsb) ? sat8(static_cast<std::int16_t>(a.s8[lane]) + b.s8[lane])
					: sat8(static_cast<std::int16_t>(a.s8[lane]) - b.s8[lane]);
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pextlw:
		case EEIROp::Pextuw:
		case EEIROp::Ppacw:
		{
			EEGPReg r = {};
			const EEGPReg& rs = state.ReadGPR(n.src0);
			const EEGPReg& rt = state.ReadGPR(n.src1);
			if (n.op == EEIROp::Pextlw)
			{
				r.u32[0] = rt.u32[0]; r.u32[1] = rs.u32[0]; r.u32[2] = rt.u32[1]; r.u32[3] = rs.u32[1];
			}
			else if (n.op == EEIROp::Pextuw)
			{
				r.u32[0] = rt.u32[2]; r.u32[1] = rs.u32[2]; r.u32[2] = rt.u32[3]; r.u32[3] = rs.u32[3];
			}
			else
			{
				r.u32[0] = rt.u32[0]; r.u32[1] = rt.u32[2]; r.u32[2] = rs.u32[0]; r.u32[3] = rs.u32[2];
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pextlh:
		case EEIROp::Pextuh:
		case EEIROp::Ppach:
		case EEIROp::Pinth:
		case EEIROp::Pinteh:
		{
			EEGPReg r = {};
			const EEGPReg& rs = state.ReadGPR(n.src0);
			const EEGPReg& rt = state.ReadGPR(n.src1);
			switch (n.op)
			{
				case EEIROp::Pextlh:
					for (int i2 = 0; i2 < 4; ++i2)
					{
						r.u16[i2 * 2] = rt.u16[i2];
						r.u16[i2 * 2 + 1] = rs.u16[i2];
					}
					break;
				case EEIROp::Pextuh:
					for (int i2 = 0; i2 < 4; ++i2)
					{
						r.u16[i2 * 2] = rt.u16[i2 + 4];
						r.u16[i2 * 2 + 1] = rs.u16[i2 + 4];
					}
					break;
				case EEIROp::Ppach:
					r.u16[0] = rt.u16[0]; r.u16[1] = rt.u16[2]; r.u16[2] = rt.u16[4]; r.u16[3] = rt.u16[6];
					r.u16[4] = rs.u16[0]; r.u16[5] = rs.u16[2]; r.u16[6] = rs.u16[4]; r.u16[7] = rs.u16[6];
					break;
				case EEIROp::Pinth:
					r.u16[0] = rt.u16[0]; r.u16[1] = rs.u16[4]; r.u16[2] = rt.u16[1]; r.u16[3] = rs.u16[5];
					r.u16[4] = rt.u16[2]; r.u16[5] = rs.u16[6]; r.u16[6] = rt.u16[3]; r.u16[7] = rs.u16[7];
					break;
				default:
					r.u16[0] = rt.u16[0]; r.u16[1] = rs.u16[0]; r.u16[2] = rt.u16[2]; r.u16[3] = rs.u16[2];
					r.u16[4] = rt.u16[4]; r.u16[5] = rs.u16[4]; r.u16[6] = rt.u16[6]; r.u16[7] = rs.u16[6];
					break;
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pextlb:
		case EEIROp::Pextub:
		case EEIROp::Ppacb:
		{
			EEGPReg r = {};
			const EEGPReg& rs = state.ReadGPR(n.src0);
			const EEGPReg& rt = state.ReadGPR(n.src1);
			if (n.op == EEIROp::Pextlb)
			{
				for (int lane = 0; lane < 8; ++lane)
				{
					r.u8[lane * 2] = rt.u8[lane];
					r.u8[lane * 2 + 1] = rs.u8[lane];
				}
			}
			else if (n.op == EEIROp::Pextub)
			{
				for (int lane = 0; lane < 8; ++lane)
				{
					r.u8[lane * 2] = rt.u8[lane + 8];
					r.u8[lane * 2 + 1] = rs.u8[lane + 8];
				}
			}
			else
			{
				static constexpr int even_bytes[8] = {0, 2, 4, 6, 8, 10, 12, 14};
				for (int lane = 0; lane < 8; ++lane)
				{
					r.u8[lane] = rt.u8[even_bytes[lane]];
					r.u8[lane + 8] = rs.u8[even_bytes[lane]];
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pext5:
		case EEIROp::Ppac5:
		{
			EEGPReg r = {};
			const EEGPReg& src = state.ReadGPR(n.src0);
			for (int lane = 0; lane < 4; ++lane)
			{
				if (n.op == EEIROp::Pext5)
				{
					r.u32[lane] = ((src.u32[lane] & 0x0000001F) << 3) | ((src.u32[lane] & 0x000003E0) << 6) |
					((src.u32[lane] & 0x00007C00) << 9) | ((src.u32[lane] & 0x00008000) << 16);
				}
				else
				{
					r.u32[lane] = ((src.u32[lane] >> 3) & 0x0000001F) | ((src.u32[lane] >> 6) & 0x000003E0) |
					((src.u32[lane] >> 9) & 0x00007C00) | ((src.u32[lane] >> 16) & 0x00008000);
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pand:
		case EEIROp::Por:
		case EEIROp::Pxor:
		case EEIROp::Pnor:
		{
			EEGPReg r = {};
			const EEGPReg& rs = state.ReadGPR(n.src0);
			const EEGPReg& rt = state.ReadGPR(n.src1);
			for (int lane = 0; lane < 2; ++lane)
			{
				if (n.op == EEIROp::Pand) r.u64[lane] = rs.u64[lane] & rt.u64[lane];
				else if (n.op == EEIROp::Por) r.u64[lane] = rs.u64[lane] | rt.u64[lane];
				else if (n.op == EEIROp::Pxor) r.u64[lane] = rs.u64[lane] ^ rt.u64[lane];
				else r.u64[lane] = ~(rs.u64[lane] | rt.u64[lane]);
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pcpyld:
		{
			EEGPReg r = {};
			r.u64[0] = state.ReadGPR(n.src1).u64[0];
			r.u64[1] = state.ReadGPR(n.src0).u64[0];
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pcpyud:
		{
			EEGPReg r = {};
			r.u64[0] = state.ReadGPR(n.src0).u64[1];
			r.u64[1] = state.ReadGPR(n.src1).u64[1];
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pcpyh:
		{
			EEGPReg r = {};
			const std::uint16_t v = state.ReadGPR(n.src0).u16[0];
			for (int lane = 0; lane < 8; ++lane)
				r.u16[lane] = v;
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pexeh:
		case EEIROp::Prevh:
		case EEIROp::Pexch:
		{
			EEGPReg r = {};
			const EEGPReg& src = state.ReadGPR(n.src0);
			if (n.op == EEIROp::Pexeh)
			{
				r.u16[0] = src.u16[2]; r.u16[1] = src.u16[1]; r.u16[2] = src.u16[0]; r.u16[3] = src.u16[3];
				r.u16[4] = src.u16[6]; r.u16[5] = src.u16[5]; r.u16[6] = src.u16[4]; r.u16[7] = src.u16[7];
			}
			else if (n.op == EEIROp::Prevh)
			{
				r.u16[0] = src.u16[3]; r.u16[1] = src.u16[2]; r.u16[2] = src.u16[1]; r.u16[3] = src.u16[0];
				r.u16[4] = src.u16[7]; r.u16[5] = src.u16[6]; r.u16[6] = src.u16[5]; r.u16[7] = src.u16[4];
			}
			else
			{
				r.u16[0] = src.u16[0]; r.u16[1] = src.u16[2]; r.u16[2] = src.u16[1]; r.u16[3] = src.u16[3];
				r.u16[4] = src.u16[4]; r.u16[5] = src.u16[6]; r.u16[6] = src.u16[5]; r.u16[7] = src.u16[7];
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pexew:
		case EEIROp::Prot3w:
		case EEIROp::Pexcw:
		{
			EEGPReg r = {};
			const EEGPReg& src = state.ReadGPR(n.src0);
			if (n.op == EEIROp::Pexew)
			{
				r.u32[0] = src.u32[2]; r.u32[1] = src.u32[1]; r.u32[2] = src.u32[0]; r.u32[3] = src.u32[3];
			}
			else if (n.op == EEIROp::Prot3w)
			{
				r.u32[0] = src.u32[1]; r.u32[1] = src.u32[2]; r.u32[2] = src.u32[0]; r.u32[3] = src.u32[3];
			}
			else
			{
				r.u32[0] = src.u32[0]; r.u32[1] = src.u32[2]; r.u32[2] = src.u32[1]; r.u32[3] = src.u32[3];
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		case EEIROp::Pmulth:
		{
			for (int lane = 0; lane < 8; ++lane)
			{
				const std::int32_t product = static_cast<std::int32_t>(state.ReadGPR(n.src0).s16[lane]) *
					static_cast<std::int32_t>(state.ReadGPR(n.src1).s16[lane]);
				if (lane < 2) state.lo.u32[lane] = static_cast<std::uint32_t>(product);
				else if (lane < 4) state.hi.u32[lane - 2] = static_cast<std::uint32_t>(product);
				else if (lane < 6) state.lo.u32[lane - 2] = static_cast<std::uint32_t>(product);
				else state.hi.u32[lane - 4] = static_cast<std::uint32_t>(product);
			}
			write_lohi_even_words(n.dst);
			break;
		}

		case EEIROp::Pmultw:
		case EEIROp::Pmultuw:
		{
			EEGPReg out = {};
			for (int lane = 0; lane < 2; ++lane)
			{
				const int src_lane = lane * 2;
				if (n.op == EEIROp::Pmultw)
				{
					const std::int64_t product = static_cast<std::int64_t>(state.ReadGPR(n.src0).s32[src_lane]) *
						static_cast<std::int64_t>(state.ReadGPR(n.src1).s32[src_lane]);
					state.lo.u64[lane] = static_cast<std::uint32_t>(product);
					state.hi.u64[lane] = static_cast<std::int32_t>(product >> 32);
					out.s64[lane] = product;
				}
				else
				{
					const std::uint64_t product = static_cast<std::uint64_t>(state.ReadGPR(n.src0).u32[src_lane]) *
						static_cast<std::uint64_t>(state.ReadGPR(n.src1).u32[src_lane]);
					state.lo.u64[lane] = static_cast<std::uint32_t>(product);
					state.hi.u64[lane] = static_cast<std::int32_t>(static_cast<std::uint32_t>(product >> 32));
					out.u64[lane] = product;
				}
			}
			if (n.dst != EEIRReg::INVALID) state.WriteGPR(n.dst, out);
			break;
		}

		case EEIROp::Pmaddw:
		{
			for (int lane = 0; lane < 2; ++lane)
			{
				const int src_lane = lane * 2;
				std::int64_t acc = static_cast<std::uint64_t>(state.lo.u32[src_lane]) |
					(static_cast<std::uint64_t>(state.hi.u32[src_lane]) << 32);
				acc += static_cast<std::int64_t>(state.ReadGPR(n.src0).s32[src_lane]) *
					static_cast<std::int64_t>(state.ReadGPR(n.src1).s32[src_lane]);
				state.lo.s64[lane] = static_cast<std::int32_t>(acc);
				state.hi.s64[lane] = static_cast<std::int32_t>(acc >> 32);
			}
			write_lohi_even_words(n.dst);
			break;
		}

		case EEIROp::Pmadduw:
		{
			EEGPReg out = {};
			for (int lane = 0; lane < 2; ++lane)
			{
				const int src_lane = lane * 2;
				std::uint64_t acc = static_cast<std::uint64_t>(state.lo.u32[src_lane]) |
					(static_cast<std::uint64_t>(state.hi.u32[src_lane]) << 32);
				acc += static_cast<std::uint64_t>(state.ReadGPR(n.src0).u32[src_lane]) *
					static_cast<std::uint64_t>(state.ReadGPR(n.src1).u32[src_lane]);
				state.lo.s64[lane] = static_cast<std::int32_t>(static_cast<std::uint32_t>(acc));
				state.hi.s64[lane] = static_cast<std::int32_t>(static_cast<std::uint32_t>(acc >> 32));
				out.u64[lane] = acc;
			}
			if (n.dst != EEIRReg::INVALID) state.WriteGPR(n.dst, out);
			break;
		}

		case EEIROp::Pmaddh:
		case EEIROp::Pmsubh:
		case EEIROp::Phmadh:
		case EEIROp::Phmsbh:
		case EEIROp::Pmsubw:
			// TODO: Complex MMI multiply/accumulate variants can be filled out further
			// from PCSX2 MMI.cpp if software execution starts depending on them.
			break;

		case EEIROp::MmiPmfhi:
			state.WriteGPR(n.dst, state.hi);
			break;

		case EEIROp::MmiPmflo:
			state.WriteGPR(n.dst, state.lo);
			break;

		case EEIROp::MmiPmthi:
			state.hi = state.ReadGPR(n.src0);
			break;

		case EEIROp::MmiPmtlo:
			state.lo = state.ReadGPR(n.src0);
			break;

		case EEIROp::Pdivw:
		case EEIROp::Pdivuw:
		{
			for (int lane = 0; lane < 2; ++lane)
			{
				const int src_lane = lane * 2;
				if (n.op == EEIROp::Pdivw)
				{
					const std::int32_t a = state.ReadGPR(n.src0).s32[src_lane];
					const std::int32_t b = state.ReadGPR(n.src1).s32[src_lane];
					if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
					{
						state.lo.s64[lane] = std::numeric_limits<std::int32_t>::min();
						state.hi.s64[lane] = 0;
					}
					else if (b != 0)
					{
						state.lo.s64[lane] = a / b;
						state.hi.s64[lane] = a % b;
					}
					else
					{
						state.lo.s64[lane] = (a < 0) ? 1 : -1;
						state.hi.s64[lane] = a;
					}
				}
				else
				{
					const std::uint32_t a = state.ReadGPR(n.src0).u32[src_lane];
					const std::uint32_t b = state.ReadGPR(n.src1).u32[src_lane];
					if (b != 0)
					{
						state.lo.s64[lane] = static_cast<std::int32_t>(a / b);
						state.hi.s64[lane] = static_cast<std::int32_t>(a % b);
					}
					else
					{
						state.lo.s64[lane] = -1;
						state.hi.s64[lane] = state.ReadGPR(n.src0).s32[src_lane];
					}
				}
			}
			break;
		}

		case EEIROp::Pdivbw:
		{
			const std::int16_t divisor = state.ReadGPR(n.src1).s16[0];
			for (int lane = 0; lane < 4; ++lane)
			{
				const std::int32_t a = state.ReadGPR(n.src0).s32[lane];
				if (a == std::numeric_limits<std::int32_t>::min() && divisor == -1)
				{
					state.lo.s32[lane] = std::numeric_limits<std::int32_t>::min();
					state.hi.s32[lane] = 0;
				}
				else if (divisor != 0)
				{
					state.lo.s32[lane] = a / divisor;
					state.hi.s32[lane] = a % divisor;
				}
				else
				{
					state.lo.s32[lane] = (a < 0) ? 1 : -1;
					state.hi.s32[lane] = a;
				}
			}
			break;
		}

		case EEIROp::Qfsrv:
		{
			const EEGPReg& rs = state.ReadGPR(n.src0);
			const EEGPReg& rt = state.ReadGPR(n.src1);
			EEGPReg r = {};
			const std::uint32_t sa_amt = state.sa << 3;
			if (sa_amt == 0)
			{
				r.u64[0] = rt.u64[0];
				r.u64[1] = rt.u64[1];
			}
			else if (sa_amt < 64)
			{
				r.u64[0] = (rt.u64[0] >> sa_amt) | (rt.u64[1] << (64 - sa_amt));
				r.u64[1] = (rt.u64[1] >> sa_amt) | (rs.u64[0] << (64 - sa_amt));
			}
			else
			{
				r.u64[0] = rt.u64[1] >> (sa_amt - 64);
				r.u64[1] = rs.u64[0] >> (sa_amt - 64);
				if (sa_amt != 64)
				{
					r.u64[0] |= rs.u64[0] << (128u - sa_amt);
					r.u64[1] |= rs.u64[1] << (128u - sa_amt);
				}
			}
			state.WriteGPR(n.dst, r);
			break;
		}

		default:
			// Unknown IR opcode — skip.
			break;

		} // switch
	} // for

	// Apply branch result.
	if (branch_taken)
		result.next_pc = branch_target;

	// Tick timers and check for pending interrupts at the block boundary.
	// A real EE would check COP0.Status.IE + COP0.Status.EXL here too, but for
	// the bootstrap interpreter we surface the flag to the caller so it can
	// decide whether to vector to the interrupt handler.
	if (hw)
	{
		// Approximate bus cycles: instructions * 2 (EE runs at ~300 MHz, bus ~150 MHz)
		hw->TickTimers(result.instructions_run * 2);
		if (hw->AnyPending())
			result.interrupt_pending = true;
	}

	return result;
}
} // namespace armsx2::wasm
