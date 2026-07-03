#pragma once

#include "ir_bytecode.h"

#include <cstdint>
#include <span>

namespace armsx2::wasm
{
// ============================================================================
// MIPS → IR lifter.
//
// Decodes raw R5900 MIPS instruction words from a byte buffer (EE memory) and
// emits EEIRBlock basic blocks.  A basic block ends when a branch, jump, or
// ERET is encountered (the delay-slot instruction is included in the block).
//
// Usage:
//   MipsLifter lifter;
//   EEIRBlock block = lifter.LiftBlock(ee_memory, memory_size, start_pc);
// ============================================================================
class MipsLifter
{
public:
	// Lift one basic block starting at `start_pc`.
	// `memory` is the flat EE memory buffer, `memory_size` its length in bytes.
	// The returned block contains the decoded IR and the PC range it covers.
	EEIRBlock LiftBlock(const std::uint8_t* memory, std::uint32_t memory_size,
						std::uint32_t start_pc) const;

private:
	// Helpers to extract MIPS instruction fields.
	static std::uint8_t  Rs(std::uint32_t op)  { return static_cast<std::uint8_t>((op >> 21) & 0x1F); }
	static std::uint8_t  Rt(std::uint32_t op)  { return static_cast<std::uint8_t>((op >> 16) & 0x1F); }
	static std::uint8_t  Rd(std::uint32_t op)  { return static_cast<std::uint8_t>((op >> 11) & 0x1F); }
	static std::uint8_t  Shamt(std::uint32_t op) { return static_cast<std::uint8_t>((op >> 6) & 0x1F); }
	static std::uint8_t  Funct(std::uint32_t op) { return static_cast<std::uint8_t>(op & 0x3F); }
	static std::int16_t  Imm16(std::uint32_t op) { return static_cast<std::int16_t>(op & 0xFFFF); }
	static std::uint16_t Imm16U(std::uint32_t op){ return static_cast<std::uint16_t>(op & 0xFFFF); }
	static std::uint32_t Target26(std::uint32_t op) { return op & 0x03FFFFFF; }

	// Decode a single MIPS word and append IR nodes to `block`.
	// Returns true if the instruction is a branch/jump (signals end of block
	// after the delay slot).
	bool DecodeInstruction(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const;

	// Sub-decoders for opcode classes.
	bool DecodeSpecial(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const;
	bool DecodeRegImm(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const;
	bool DecodeCOP0(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const;
	bool DecodeCOP1(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const;
	bool DecodeMMI(std::uint32_t op, std::uint32_t pc, EEIRBlock& block) const;
};
} // namespace armsx2::wasm
