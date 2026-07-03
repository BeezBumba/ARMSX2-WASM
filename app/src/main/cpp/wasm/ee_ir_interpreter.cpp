#include "ee_ir_interpreter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
} // anonymous namespace

std::uint8_t EEIRInterpreter::ReadMem8(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	const std::uint32_t pa = ToPhys(vaddr);
	return (pa < sz) ? mem[pa] : 0;
}

std::uint16_t EEIRInterpreter::ReadMem16(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 2 > sz) return 0;
	std::uint16_t v;
	std::memcpy(&v, mem + pa, 2);
	return v;
}

std::uint32_t EEIRInterpreter::ReadMem32(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 4 > sz) return 0;
	std::uint32_t v;
	std::memcpy(&v, mem + pa, 4);
	return v;
}

std::uint64_t EEIRInterpreter::ReadMem64(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr)
{
	const std::uint32_t pa = ToPhys(vaddr);
	if (pa + 8 > sz) return 0;
	std::uint64_t v;
	std::memcpy(&v, mem + pa, 8);
	return v;
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
	std::uint8_t* ee_memory, std::uint32_t ee_memory_size) const
{
	RunResult result;
	result.next_pc = block.end_pc; // default: fall through

	// Delay-slot tracking.  When a branch IR node is encountered we record the
	// target but don't change PC until the block ends.
	bool branch_taken = false;
	std::uint32_t branch_target = 0;

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
			// For now, just halt — real syscall handling requires BIOS HLE.
			result.halted = true;
			result.error = "SYSCALL encountered (code=" + std::to_string(n.imm) + ")";
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
			state.WriteGPR32(n.dst, static_cast<std::int32_t>(ReadMem32(ee_memory, ee_memory_size, addr)));
			break;
		}

		case EEIROp::LWU:
		{
			const std::uint32_t addr = state.ReadGPR(n.src0).u32[0] + static_cast<std::int16_t>(n.imm);
			state.WriteGPR64(n.dst, static_cast<std::int64_t>(static_cast<std::uint32_t>(ReadMem32(ee_memory, ee_memory_size, addr))));
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
				state.WriteGPR32(n.dst, static_cast<std::int32_t>(ReadMem32(ee_memory, ee_memory_size, addr & ~3u)));
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
			WriteMem32(ee_memory, ee_memory_size, addr, state.ReadGPR(n.src0).u32[0]);
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
			WriteMem32(ee_memory, ee_memory_size, addr & ~3u, state.ReadGPR(n.src0).u32[0]);
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
				// Branch offset is relative to the delay-slot PC, which is pc+4 from the branch.
				// The lifter stored (imm16 * 4) as the offset relative to the delay-slot PC.
				// We compute: target = block.start_pc + (branch_instruction_index * 4) + 4 + offset
				// But for simplicity, since we embedded the offset, we use end_pc of the block
				// minus 4 (the delay slot) as the reference PC.
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
				state.fpr[fpr_idx].u32 = ReadMem32(ee_memory, ee_memory_size, addr);
			break;
		}

		case EEIROp::SWC1:
		{
			const std::uint32_t addr = state.ReadGPR(n.dst).u32[0] + static_cast<std::int16_t>(n.imm);
			const unsigned fpr_idx = n.src0 - 64;
			if (fpr_idx < 32)
				WriteMem32(ee_memory, ee_memory_size, addr, state.fpr[fpr_idx].u32);
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

		default:
			// Unknown IR opcode — skip.
			break;

		} // switch
	} // for

	// Apply branch result.
	if (branch_taken)
		result.next_pc = branch_target;

	return result;
}
} // namespace armsx2::wasm
