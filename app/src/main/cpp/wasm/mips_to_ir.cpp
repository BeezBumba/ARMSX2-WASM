#include "mips_to_ir.h"

#include <cstdio>
#include <cstring>
#include <sstream>

namespace armsx2::wasm
{
// ============================================================================
// EEIROpName — debug helper
// ============================================================================
const char* EEIROpName(EEIROp op)
{
	switch (op)
	{
		case EEIROp::Nop:       return "Nop";
		case EEIROp::Halt:      return "Halt";
		case EEIROp::BlockEnd:  return "BlockEnd";
		case EEIROp::Syscall:   return "Syscall";
		case EEIROp::Break:     return "Break";
		case EEIROp::Eret:      return "Eret";

		case EEIROp::Add:       return "Add";
		case EEIROp::AddU:      return "AddU";
		case EEIROp::Sub:       return "Sub";
		case EEIROp::SubU:      return "SubU";
		case EEIROp::AddImm:    return "AddImm";
		case EEIROp::AddImmU:   return "AddImmU";

		case EEIROp::DAdd:      return "DAdd";
		case EEIROp::DAddU:     return "DAddU";
		case EEIROp::DSub:      return "DSub";
		case EEIROp::DSubU:     return "DSubU";
		case EEIROp::DAddImm:   return "DAddImm";
		case EEIROp::DAddImmU:  return "DAddImmU";

		case EEIROp::And:       return "And";
		case EEIROp::Or:        return "Or";
		case EEIROp::Xor:       return "Xor";
		case EEIROp::Nor:       return "Nor";
		case EEIROp::AndImm:    return "AndImm";
		case EEIROp::OrImm:     return "OrImm";
		case EEIROp::XorImm:    return "XorImm";

		case EEIROp::Slt:       return "Slt";
		case EEIROp::SltU:      return "SltU";
		case EEIROp::SltImm:    return "SltImm";
		case EEIROp::SltImmU:   return "SltImmU";

		case EEIROp::Sll:       return "Sll";
		case EEIROp::Srl:       return "Srl";
		case EEIROp::Sra:       return "Sra";
		case EEIROp::SllV:      return "SllV";
		case EEIROp::SrlV:      return "SrlV";
		case EEIROp::SraV:      return "SraV";

		case EEIROp::DSll:      return "DSll";
		case EEIROp::DSrl:      return "DSrl";
		case EEIROp::DSra:      return "DSra";
		case EEIROp::DSll32:    return "DSll32";
		case EEIROp::DSrl32:    return "DSrl32";
		case EEIROp::DSra32:    return "DSra32";
		case EEIROp::DSllV:     return "DSllV";
		case EEIROp::DSrlV:     return "DSrlV";
		case EEIROp::DSraV:     return "DSraV";

		case EEIROp::Mult:      return "Mult";
		case EEIROp::MultU:     return "MultU";
		case EEIROp::Div:       return "Div";
		case EEIROp::DivU:      return "DivU";

		case EEIROp::Lui:       return "Lui";

		case EEIROp::Mfhi:      return "Mfhi";
		case EEIROp::Mthi:      return "Mthi";
		case EEIROp::Mflo:      return "Mflo";
		case EEIROp::Mtlo:      return "Mtlo";
		case EEIROp::Mfsa:      return "Mfsa";
		case EEIROp::Mtsa:      return "Mtsa";
		case EEIROp::Mtsab:     return "Mtsab";
		case EEIROp::Mtsah:     return "Mtsah";

		case EEIROp::MovZ:      return "MovZ";
		case EEIROp::MovN:      return "MovN";

		case EEIROp::LB:        return "LB";
		case EEIROp::LBU:       return "LBU";
		case EEIROp::LH:        return "LH";
		case EEIROp::LHU:       return "LHU";
		case EEIROp::LW:        return "LW";
		case EEIROp::LWU:       return "LWU";
		case EEIROp::LD:        return "LD";
		case EEIROp::LQ:        return "LQ";
		case EEIROp::LWL:       return "LWL";
		case EEIROp::LWR:       return "LWR";
		case EEIROp::LDL:       return "LDL";
		case EEIROp::LDR:       return "LDR";

		case EEIROp::SB:        return "SB";
		case EEIROp::SH:        return "SH";
		case EEIROp::SW:        return "SW";
		case EEIROp::SD:        return "SD";
		case EEIROp::SQ:        return "SQ";
		case EEIROp::SWL:       return "SWL";
		case EEIROp::SWR:       return "SWR";
		case EEIROp::SDL:       return "SDL";
		case EEIROp::SDR:       return "SDR";

		case EEIROp::Beq:       return "Beq";
		case EEIROp::Bne:       return "Bne";
		case EEIROp::Blez:      return "Blez";
		case EEIROp::Bgtz:      return "Bgtz";
		case EEIROp::Bltz:      return "Bltz";
		case EEIROp::Bgez:      return "Bgez";
		case EEIROp::BeqL:      return "BeqL";
		case EEIROp::BneL:      return "BneL";
		case EEIROp::BlezL:     return "BlezL";
		case EEIROp::BgtzL:     return "BgtzL";
		case EEIROp::BltzL:     return "BltzL";
		case EEIROp::BgezL:     return "BgezL";
		case EEIROp::Bgezal:    return "Bgezal";
		case EEIROp::Bltzal:    return "Bltzal";
		case EEIROp::BgezalL:   return "BgezalL";
		case EEIROp::BltzalL:   return "BltzalL";

		case EEIROp::J:         return "J";
		case EEIROp::Jal:       return "Jal";
		case EEIROp::Jr:        return "Jr";
		case EEIROp::Jalr:      return "Jalr";

		case EEIROp::Mfc0:      return "Mfc0";
		case EEIROp::Mtc0:      return "Mtc0";
		case EEIROp::Tlbr:      return "Tlbr";
		case EEIROp::Tlbwi:     return "Tlbwi";
		case EEIROp::Tlbwr:     return "Tlbwr";
		case EEIROp::Tlbp:      return "Tlbp";
		case EEIROp::Di:        return "Di";
		case EEIROp::Ei:        return "Ei";
		case EEIROp::Eret:      return "Eret";

		case EEIROp::Mfc1:      return "Mfc1";
		case EEIROp::Mtc1:      return "Mtc1";
		case EEIROp::Cfc1:      return "Cfc1";
		case EEIROp::Ctc1:      return "Ctc1";
		case EEIROp::FAdd:      return "FAdd";
		case EEIROp::FSub:      return "FSub";
		case EEIROp::FMul:      return "FMul";
		case EEIROp::FDiv:      return "FDiv";
		case EEIROp::FSqrt:     return "FSqrt";
		case EEIROp::FAbs:      return "FAbs";
		case EEIROp::FNeg:      return "FNeg";
		case EEIROp::FMov:      return "FMov";
		case EEIROp::CvtSW:     return "CvtSW";
		case EEIROp::CvtWS:     return "CvtWS";
		case EEIROp::FCmpEQ:    return "FCmpEQ";
		case EEIROp::FCmpLT:    return "FCmpLT";
		case EEIROp::FCmpLE:    return "FCmpLE";
		case EEIROp::BC1F:      return "BC1F";
		case EEIROp::BC1T:      return "BC1T";
		case EEIROp::BC1FL:     return "BC1FL";
		case EEIROp::BC1TL:     return "BC1TL";
		case EEIROp::LWC1:      return "LWC1";
		case EEIROp::SWC1:      return "SWC1";
		case EEIROp::FMadd:     return "FMadd";
		case EEIROp::FMsub:     return "FMsub";
		case EEIROp::FMadda:    return "FMadda";
		case EEIROp::FMsuba:    return "FMsuba";
		case EEIROp::FAdda:     return "FAdda";
		case EEIROp::FSuba:     return "FSuba";
		case EEIROp::FMula:     return "FMula";
		case EEIROp::FMax:      return "FMax";
		case EEIROp::FMin:      return "FMin";
		case EEIROp::FRsqrt:    return "FRsqrt";

		case EEIROp::Cache:     return "Cache";
		case EEIROp::Pref:      return "Pref";
		case EEIROp::Sync:      return "Sync";

		case EEIROp::Tge:       return "Tge";
		case EEIROp::TgeU:      return "TgeU";
		case EEIROp::Tlt:       return "Tlt";
		case EEIROp::TltU:      return "TltU";
		case EEIROp::Teq:       return "Teq";
		case EEIROp::Tne:       return "Tne";
		case EEIROp::TgeImm:    return "TgeImm";
		case EEIROp::TgeImmU:   return "TgeImmU";
		case EEIROp::TltImm:    return "TltImm";
		case EEIROp::TltImmU:   return "TltImmU";
		case EEIROp::TeqImm:    return "TeqImm";
		case EEIROp::TneImm:    return "TneImm";

		case EEIROp::COP2:      return "COP2";
		case EEIROp::LQC2:      return "LQC2";
		case EEIROp::SQC2:      return "SQC2";

		default:                return "???";
	}
}

// ============================================================================
// EEIRBlock::Dump — debug dump
// ============================================================================
std::string EEIRBlock::Dump() const
{
	std::ostringstream out;
	out << "EEIRBlock [" << std::hex << start_pc << ".." << end_pc << ") "
		<< std::dec << instructions.size() << " IR nodes\n";
	for (size_t i = 0; i < instructions.size(); i++)
	{
		const auto& n = instructions[i];
		out << "  [" << i << "] " << EEIROpName(n.op)
			<< " dst=" << (int)n.dst
			<< " src0=" << (int)n.src0
			<< " src1=" << (int)n.src1
			<< " imm=" << n.imm << "\n";
	}
	return out.str();
}

// ============================================================================
// Helper: emit one IR instruction.
// ============================================================================
namespace
{
void Emit(EEIRBlock& block, EEIROp op, std::uint8_t dst = EEIRReg::INVALID,
		  std::uint8_t src0 = EEIRReg::INVALID, std::uint8_t src1 = EEIRReg::INVALID,
		  std::int64_t imm = 0)
{
	block.instructions.push_back({op, dst, src0, src1, imm});
}

// Read a 32-bit little-endian word from memory at a physical address.
std::uint32_t ReadWord(const std::uint8_t* memory, std::uint32_t memory_size, std::uint32_t phys_addr)
{
	if (phys_addr + 4 > memory_size)
		return 0;
	std::uint32_t w;
	std::memcpy(&w, memory + phys_addr, sizeof(w));
	return w;
}
} // anonymous namespace

// ============================================================================
// LiftBlock — decode a basic block of MIPS instructions into IR.
// ============================================================================
EEIRBlock MipsLifter::LiftBlock(const std::uint8_t* memory, std::uint32_t memory_size,
								std::uint32_t start_pc) const
{
	EEIRBlock block;
	block.start_pc = start_pc;

	std::uint32_t pc = start_pc;
	bool in_delay_slot = false;
	bool block_done = false;

	// Limit block size to avoid runaway decoding.
	constexpr std::uint32_t max_instructions = 512;
	std::uint32_t decoded = 0;

	while (!block_done && decoded < max_instructions)
	{
		// Convert virtual address to physical (simple mask for kseg0/kseg1).
		std::uint32_t phys = pc & 0x1FFFFFFF;
		std::uint32_t op = ReadWord(memory, memory_size, phys);

		bool is_branch = DecodeInstruction(op, pc, block);

		pc += 4;
		decoded++;

		if (in_delay_slot)
		{
			// We just decoded the delay slot — block is done.
			block_done = true;
		}
		else if (is_branch)
		{
			// Next instruction is the delay slot.
			in_delay_slot = true;
		}
	}

	if (!block_done)
	{
		// Hit the instruction limit — end the block.
		Emit(block, EEIROp::BlockEnd, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID,
			 static_cast<std::int64_t>(pc));
	}

	block.end_pc = pc;
	return block;
}

// ============================================================================
// DecodeInstruction — main opcode dispatch (6-bit primary opcode).
// ============================================================================
bool MipsLifter::DecodeInstruction(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const
{
	if (op == 0)
	{
		Emit(block, EEIROp::Nop);
		return false;
	}

	const std::uint8_t primary = static_cast<std::uint8_t>((op >> 26) & 0x3F);

	switch (primary)
	{
		case 0x00: return DecodeSpecial(op, pc, block);
		case 0x01: return DecodeRegImm(op, pc, block);

		// Jumps
		case 0x02: // J
			Emit(block, EEIROp::J, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID,
				 static_cast<std::int64_t>(Target26(op)));
			return true;

		case 0x03: // JAL
			Emit(block, EEIROp::Jal, EEIRReg::GPR(31), EEIRReg::INVALID, EEIRReg::INVALID,
				 static_cast<std::int64_t>(Target26(op)));
			return true;

		// Branches
		case 0x04: // BEQ
			Emit(block, EEIROp::Beq, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)),
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		case 0x05: // BNE
			Emit(block, EEIROp::Bne, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)),
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		case 0x06: // BLEZ
			Emit(block, EEIROp::Blez, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		case 0x07: // BGTZ
			Emit(block, EEIROp::Bgtz, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		// ALU immediate
		case 0x08: // ADDI
			Emit(block, EEIROp::AddImm, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x09: // ADDIU
			Emit(block, EEIROp::AddImmU, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x0A: // SLTI
			Emit(block, EEIROp::SltImm, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x0B: // SLTIU
			Emit(block, EEIROp::SltImmU, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x0C: // ANDI
			Emit(block, EEIROp::AndImm, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16U(op)));
			return false;

		case 0x0D: // ORI
			Emit(block, EEIROp::OrImm, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16U(op)));
			return false;

		case 0x0E: // XORI
			Emit(block, EEIROp::XorImm, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16U(op)));
			return false;

		case 0x0F: // LUI
			Emit(block, EEIROp::Lui, EEIRReg::GPR(Rt(op)), EEIRReg::INVALID, EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x10: return DecodeCOP0(op, pc, block);
		case 0x11: return DecodeCOP1(op, pc, block);

		case 0x12: // COP2 placeholder
			Emit(block, EEIROp::COP2, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID,
				 static_cast<std::int64_t>(op));
			return false;

		// Likely branches
		case 0x14: // BEQL
			Emit(block, EEIROp::BeqL, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)),
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		case 0x15: // BNEL
			Emit(block, EEIROp::BneL, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)),
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		case 0x16: // BLEZL
			Emit(block, EEIROp::BlezL, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		case 0x17: // BGTZL
			Emit(block, EEIROp::BgtzL, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)) * 4);
			return true;

		// 64-bit ALU immediate
		case 0x18: // DADDI
			Emit(block, EEIROp::DAddImm, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x19: // DADDIU
			Emit(block, EEIROp::DAddImmU, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		// Unaligned loads
		case 0x1A: // LDL
			Emit(block, EEIROp::LDL, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x1B: // LDR
			Emit(block, EEIROp::LDR, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x1C: return DecodeMMI(op, pc, block);

		case 0x1E: // LQ
			Emit(block, EEIROp::LQ, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x1F: // SQ
			Emit(block, EEIROp::SQ, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		// Standard loads
		case 0x20: // LB
			Emit(block, EEIROp::LB, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x21: // LH
			Emit(block, EEIROp::LH, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x22: // LWL
			Emit(block, EEIROp::LWL, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x23: // LW
			Emit(block, EEIROp::LW, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x24: // LBU
			Emit(block, EEIROp::LBU, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x25: // LHU
			Emit(block, EEIROp::LHU, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x26: // LWR
			Emit(block, EEIROp::LWR, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x27: // LWU
			Emit(block, EEIROp::LWU, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		// Standard stores
		case 0x28: // SB
			Emit(block, EEIROp::SB, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x29: // SH
			Emit(block, EEIROp::SH, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x2A: // SWL
			Emit(block, EEIROp::SWL, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x2B: // SW
			Emit(block, EEIROp::SW, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x2C: // SDL
			Emit(block, EEIROp::SDL, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x2D: // SDR
			Emit(block, EEIROp::SDR, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x2E: // SWR
			Emit(block, EEIROp::SWR, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x2F: // CACHE
			Emit(block, EEIROp::Cache);
			return false;

		// COP1 load/store
		case 0x31: // LWC1
			Emit(block, EEIROp::LWC1, EEIRReg::FPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x33: // PREF
			Emit(block, EEIROp::Pref);
			return false;

		case 0x36: // LQC2
			Emit(block, EEIROp::LQC2, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(op));
			return false;

		case 0x37: // LD
			Emit(block, EEIROp::LD, EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x39: // SWC1
			Emit(block, EEIROp::SWC1, EEIRReg::GPR(Rs(op)), EEIRReg::FPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x3E: // SQC2
			Emit(block, EEIROp::SQC2, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(op));
			return false;

		case 0x3F: // SD
			Emit(block, EEIROp::SD, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		default:
			// Unknown / unimplemented primary opcode — emit nop and continue.
			Emit(block, EEIROp::Nop);
			return false;
	}
}

// ============================================================================
// SPECIAL (opcode 0x00) — ALU register-register, shifts, jumps, etc.
// ============================================================================
bool MipsLifter::DecodeSpecial(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const
{
	const std::uint8_t funct = Funct(op);

	switch (funct)
	{
		case 0x00: // SLL
			Emit(block, EEIROp::Sll, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x02: // SRL
			Emit(block, EEIROp::Srl, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x03: // SRA
			Emit(block, EEIROp::Sra, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x04: // SLLV
			Emit(block, EEIROp::SllV, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)));
			return false;

		case 0x06: // SRLV
			Emit(block, EEIROp::SrlV, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)));
			return false;

		case 0x07: // SRAV
			Emit(block, EEIROp::SraV, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)));
			return false;

		case 0x08: // JR
			Emit(block, EEIROp::Jr, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)));
			return true;

		case 0x09: // JALR
			Emit(block, EEIROp::Jalr, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)));
			return true;

		case 0x0A: // MOVZ
			Emit(block, EEIROp::MovZ, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x0B: // MOVN
			Emit(block, EEIROp::MovN, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x0C: // SYSCALL
			Emit(block, EEIROp::Syscall, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID,
				 static_cast<std::int64_t>((op >> 6) & 0xFFFFF));
			return true;

		case 0x0D: // BREAK
			Emit(block, EEIROp::Break);
			return true;

		case 0x0F: // SYNC
			Emit(block, EEIROp::Sync);
			return false;

		case 0x10: // MFHI
			Emit(block, EEIROp::Mfhi, EEIRReg::GPR(Rd(op)), EEIRReg::HI);
			return false;

		case 0x11: // MTHI
			Emit(block, EEIROp::Mthi, EEIRReg::HI, EEIRReg::GPR(Rs(op)));
			return false;

		case 0x12: // MFLO
			Emit(block, EEIROp::Mflo, EEIRReg::GPR(Rd(op)), EEIRReg::LO);
			return false;

		case 0x13: // MTLO
			Emit(block, EEIROp::Mtlo, EEIRReg::LO, EEIRReg::GPR(Rs(op)));
			return false;

		case 0x14: // DSLLV
			Emit(block, EEIROp::DSllV, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)));
			return false;

		case 0x16: // DSRLV
			Emit(block, EEIROp::DSrlV, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)));
			return false;

		case 0x17: // DSRAV
			Emit(block, EEIROp::DSraV, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::GPR(Rs(op)));
			return false;

		case 0x18: // MULT
			Emit(block, EEIROp::Mult, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x19: // MULTU
			Emit(block, EEIROp::MultU, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x1A: // DIV
			Emit(block, EEIROp::Div, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x1B: // DIVU
			Emit(block, EEIROp::DivU, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x20: // ADD
			Emit(block, EEIROp::Add, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x21: // ADDU
			Emit(block, EEIROp::AddU, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x22: // SUB
			Emit(block, EEIROp::Sub, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x23: // SUBU
			Emit(block, EEIROp::SubU, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x24: // AND
			Emit(block, EEIROp::And, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x25: // OR
			Emit(block, EEIROp::Or, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x26: // XOR
			Emit(block, EEIROp::Xor, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x27: // NOR
			Emit(block, EEIROp::Nor, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x28: // MFSA
			Emit(block, EEIROp::Mfsa, EEIRReg::GPR(Rd(op)), EEIRReg::SA);
			return false;

		case 0x29: // MTSA
			Emit(block, EEIROp::Mtsa, EEIRReg::SA, EEIRReg::GPR(Rs(op)));
			return false;

		case 0x2A: // SLT
			Emit(block, EEIROp::Slt, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x2B: // SLTU
			Emit(block, EEIROp::SltU, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x2C: // DADD
			Emit(block, EEIROp::DAdd, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x2D: // DADDU
			Emit(block, EEIROp::DAddU, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x2E: // DSUB
			Emit(block, EEIROp::DSub, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x2F: // DSUBU
			Emit(block, EEIROp::DSubU, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op)));
			return false;

		// Traps
		case 0x30: Emit(block, EEIROp::Tge, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op))); return false;
		case 0x31: Emit(block, EEIROp::TgeU, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op))); return false;
		case 0x32: Emit(block, EEIROp::Tlt, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op))); return false;
		case 0x33: Emit(block, EEIROp::TltU, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op))); return false;
		case 0x34: Emit(block, EEIROp::Teq, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op))); return false;
		case 0x36: Emit(block, EEIROp::Tne, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::GPR(Rt(op))); return false;

		// 64-bit shifts
		case 0x38: // DSLL
			Emit(block, EEIROp::DSll, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x3A: // DSRL
			Emit(block, EEIROp::DSrl, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x3B: // DSRA
			Emit(block, EEIROp::DSra, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x3C: // DSLL32
			Emit(block, EEIROp::DSll32, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x3E: // DSRL32
			Emit(block, EEIROp::DSrl32, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		case 0x3F: // DSRA32
			Emit(block, EEIROp::DSra32, EEIRReg::GPR(Rd(op)), EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Shamt(op)));
			return false;

		default:
			Emit(block, EEIROp::Nop);
			return false;
	}
}

// ============================================================================
// REGIMM (opcode 0x01) — branch-on-register instructions.
// ============================================================================
bool MipsLifter::DecodeRegImm(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const
{
	const std::uint8_t rt = Rt(op);
	const std::int64_t offset = static_cast<std::int64_t>(Imm16(op)) * 4;

	switch (rt)
	{
		case 0x00: // BLTZ
			Emit(block, EEIROp::Bltz, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x01: // BGEZ
			Emit(block, EEIROp::Bgez, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x02: // BLTZL
			Emit(block, EEIROp::BltzL, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x03: // BGEZL
			Emit(block, EEIROp::BgezL, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		// Trap immediate
		case 0x08: Emit(block, EEIROp::TgeImm, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, static_cast<std::int64_t>(Imm16(op))); return false;
		case 0x09: Emit(block, EEIROp::TgeImmU, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, static_cast<std::int64_t>(Imm16(op))); return false;
		case 0x0A: Emit(block, EEIROp::TltImm, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, static_cast<std::int64_t>(Imm16(op))); return false;
		case 0x0B: Emit(block, EEIROp::TltImmU, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, static_cast<std::int64_t>(Imm16(op))); return false;
		case 0x0C: Emit(block, EEIROp::TeqImm, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, static_cast<std::int64_t>(Imm16(op))); return false;
		case 0x0E: Emit(block, EEIROp::TneImm, EEIRReg::INVALID, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, static_cast<std::int64_t>(Imm16(op))); return false;

		case 0x10: // BLTZAL
			Emit(block, EEIROp::Bltzal, EEIRReg::GPR(31), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x11: // BGEZAL
			Emit(block, EEIROp::Bgezal, EEIRReg::GPR(31), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x12: // BLTZALL
			Emit(block, EEIROp::BltzalL, EEIRReg::GPR(31), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x13: // BGEZALL
			Emit(block, EEIROp::BgezalL, EEIRReg::GPR(31), EEIRReg::GPR(Rs(op)), EEIRReg::INVALID, offset);
			return true;

		case 0x18: // MTSAB
			Emit(block, EEIROp::Mtsab, EEIRReg::SA, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		case 0x19: // MTSAH
			Emit(block, EEIROp::Mtsah, EEIRReg::SA, EEIRReg::GPR(Rs(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Imm16(op)));
			return false;

		default:
			Emit(block, EEIROp::Nop);
			return false;
	}
}

// ============================================================================
// COP0 (opcode 0x10) — system control coprocessor.
// ============================================================================
bool MipsLifter::DecodeCOP0(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const
{
	const std::uint8_t rs = Rs(op);

	switch (rs)
	{
		case 0x00: // MFC0
			Emit(block, EEIROp::Mfc0, EEIRReg::GPR(Rt(op)), EEIRReg::COP0(Rd(op)));
			return false;

		case 0x04: // MTC0
			Emit(block, EEIROp::Mtc0, EEIRReg::COP0(Rd(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x08: // BC0 — not commonly used, treat as nop for now
			Emit(block, EEIROp::Nop);
			return false;

		case 0x10: // C0 (TLB / ERET)
		{
			const std::uint8_t funct = Funct(op);
			switch (funct)
			{
				case 0x01: Emit(block, EEIROp::Tlbr); return false;
				case 0x02: Emit(block, EEIROp::Tlbwi); return false;
				case 0x06: Emit(block, EEIROp::Tlbwr); return false;
				case 0x08: Emit(block, EEIROp::Tlbp); return false;
				case 0x18: Emit(block, EEIROp::Eret); return true;
				case 0x38: Emit(block, EEIROp::Ei); return false;
				case 0x39: Emit(block, EEIROp::Di); return false;
				default:   Emit(block, EEIROp::Nop); return false;
			}
		}

		default:
			Emit(block, EEIROp::Nop);
			return false;
	}
}

// ============================================================================
// COP1 (opcode 0x11) — floating-point unit.
// ============================================================================
bool MipsLifter::DecodeCOP1(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const
{
	const std::uint8_t rs = Rs(op);

	switch (rs)
	{
		case 0x00: // MFC1
			Emit(block, EEIROp::Mfc1, EEIRReg::GPR(Rt(op)), EEIRReg::FPR(Rd(op)));
			return false;

		case 0x02: // CFC1
			Emit(block, EEIROp::Cfc1, EEIRReg::GPR(Rt(op)), EEIRReg::INVALID, EEIRReg::INVALID,
				 static_cast<std::int64_t>(Rd(op)));
			return false;

		case 0x04: // MTC1
			Emit(block, EEIROp::Mtc1, EEIRReg::FPR(Rd(op)), EEIRReg::GPR(Rt(op)));
			return false;

		case 0x06: // CTC1
			Emit(block, EEIROp::Ctc1, EEIRReg::INVALID, EEIRReg::GPR(Rt(op)), EEIRReg::INVALID,
				 static_cast<std::int64_t>(Rd(op)));
			return false;

		case 0x08: // BC1
		{
			const std::uint8_t bc = Rt(op);
			const std::int64_t offset = static_cast<std::int64_t>(Imm16(op)) * 4;
			switch (bc & 0x03)
			{
				case 0x00: Emit(block, EEIROp::BC1F, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID, offset); return true;
				case 0x01: Emit(block, EEIROp::BC1T, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID, offset); return true;
				case 0x02: Emit(block, EEIROp::BC1FL, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID, offset); return true;
				case 0x03: Emit(block, EEIROp::BC1TL, EEIRReg::INVALID, EEIRReg::INVALID, EEIRReg::INVALID, offset); return true;
				default: Emit(block, EEIROp::Nop); return false;
			}
		}

		case 0x10: // S (single-precision)
		{
			const std::uint8_t funct = Funct(op);
			const std::uint8_t fd = EEIRReg::FPR(Shamt(op));  // COP1 fd field = bits 10:6
			const std::uint8_t fs = EEIRReg::FPR(Rd(op));     // COP1 fs field = bits 15:11
			const std::uint8_t ft = EEIRReg::FPR(Rt(op));     // COP1 ft field = bits 20:16

			switch (funct)
			{
				case 0x00: Emit(block, EEIROp::FAdd, fd, fs, ft); return false;
				case 0x01: Emit(block, EEIROp::FSub, fd, fs, ft); return false;
				case 0x02: Emit(block, EEIROp::FMul, fd, fs, ft); return false;
				case 0x03: Emit(block, EEIROp::FDiv, fd, fs, ft); return false;
				case 0x04: Emit(block, EEIROp::FSqrt, fd, ft); return false;
				case 0x05: Emit(block, EEIROp::FAbs, fd, fs); return false;
				case 0x06: Emit(block, EEIROp::FMov, fd, fs); return false;
				case 0x07: Emit(block, EEIROp::FNeg, fd, fs); return false;

				case 0x16: Emit(block, EEIROp::FRsqrt, fd, fs, ft); return false;

				case 0x18: Emit(block, EEIROp::FAdda, EEIRReg::FPR_ACC, fs, ft); return false;
				case 0x19: Emit(block, EEIROp::FSuba, EEIRReg::FPR_ACC, fs, ft); return false;
				case 0x1A: Emit(block, EEIROp::FMula, EEIRReg::FPR_ACC, fs, ft); return false;

				case 0x1C: Emit(block, EEIROp::FMadd, fd, fs, ft); return false;
				case 0x1D: Emit(block, EEIROp::FMsub, fd, fs, ft); return false;
				case 0x1E: Emit(block, EEIROp::FMadda, EEIRReg::FPR_ACC, fs, ft); return false;
				case 0x1F: Emit(block, EEIROp::FMsuba, EEIRReg::FPR_ACC, fs, ft); return false;

				case 0x24: Emit(block, EEIROp::CvtWS, fd, fs); return false;

				case 0x28: Emit(block, EEIROp::FMax, fd, fs, ft); return false;
				case 0x29: Emit(block, EEIROp::FMin, fd, fs, ft); return false;

				case 0x32: Emit(block, EEIROp::FCmpEQ, EEIRReg::INVALID, fs, ft); return false;
				case 0x34: Emit(block, EEIROp::FCmpLT, EEIRReg::INVALID, fs, ft); return false;
				case 0x36: Emit(block, EEIROp::FCmpLE, EEIRReg::INVALID, fs, ft); return false;

				default:
					Emit(block, EEIROp::Nop);
					return false;
			}
		}

		case 0x14: // W (word — int to float conversion)
		{
			const std::uint8_t funct = Funct(op);
			const std::uint8_t fd = EEIRReg::FPR(Shamt(op));
			const std::uint8_t fs = EEIRReg::FPR(Rd(op));
			if (funct == 0x20) // CVT.S.W
			{
				Emit(block, EEIROp::CvtSW, fd, fs);
				return false;
			}
			Emit(block, EEIROp::Nop);
			return false;
		}

		default:
			Emit(block, EEIROp::Nop);
			return false;
	}
}

// ============================================================================
// MMI (opcode 0x1C) — Multimedia Instructions.
// Placeholder: emits Nop for now — individual MMI ops will be expanded as
// the SIMD IR is designed.
// ============================================================================
bool MipsLifter::DecodeMMI(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const
{
	// MMI instructions use SPECIAL2 encoding (function field at bits 5:0).
	// For now, emit a Nop placeholder.  The full 128-bit SIMD IR nodes
	// (PADDB, PADDW, PAND, etc.) will be added in a later phase.
	Emit(block, EEIROp::Nop);
	return false;
}
} // namespace armsx2::wasm
