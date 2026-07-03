#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace armsx2::wasm
{
// ============================================================================
// Legacy bootstrap IR opcodes (kept for the SDL color demo).
// ============================================================================
enum class IROpcode : std::uint8_t
{
	Nop,
	LoadImmF32,
	AddF32,
	StoreColor,
	Halt,
};

struct IRInstruction
{
	IROpcode opcode = IROpcode::Nop;
	std::uint8_t dst = 0;
	std::uint8_t src0 = 0;
	std::uint8_t src1 = 0;
	float immediate = 0.0f;
};

struct IRProgram
{
	std::vector<IRInstruction> instructions;
	std::array<float, 4> color = {0.08f, 0.10f, 0.16f, 1.0f};
};

// ============================================================================
// PS2 EE IR — architecture-neutral intermediate representation for R5900 MIPS
// instructions.  The lifter (mips_to_ir) decodes raw MIPS words from EE memory
// and emits sequences of these nodes.  The interpreter (ee_ir_interpreter) or a
// future WASM emitter consumes them.
//
// Design:
//   * Each node operates on *virtual register indices* that map 1-to-1 with the
//     EEState fields (GPR 0-31, HI=32, LO=33, PC=34, SA=35, FPR 0-31 → 64-95).
//   * Immediates are carried inline in `EEIRInstruction::imm`.
//   * Memory accesses go through a simple flat model (physical address = vaddr &
//     0x1FFF'FFFF) matching the bootstrap wasm_memory helpers.  Full vtlb can be
//     layered on later.
// ============================================================================

// Virtual register mapping for EEIRInstruction dst/src fields.
namespace EEIRReg
{
	// GPR r0..r31 → 0..31
	constexpr std::uint8_t GPR(unsigned i) { return static_cast<std::uint8_t>(i & 31); }

	constexpr std::uint8_t HI = 32;
	constexpr std::uint8_t LO = 33;
	constexpr std::uint8_t PC = 34;
	constexpr std::uint8_t SA = 35;

	// HI1/LO1 are the upper 64-bit halves used by some R5900 mult instructions.
	constexpr std::uint8_t HI1 = 36;
	constexpr std::uint8_t LO1 = 37;

	// FPR f0..f31 → 64..95
	constexpr std::uint8_t FPR(unsigned i) { return static_cast<std::uint8_t>(64 + (i & 31)); }
	constexpr std::uint8_t FPR_ACC = 96;  // FPU accumulator
	constexpr std::uint8_t FPR_ACCflag = 97;

	// COP0 registers → 128..159
	constexpr std::uint8_t COP0(unsigned i) { return static_cast<std::uint8_t>(128 + (i & 31)); }

	constexpr std::uint8_t INVALID = 255;
} // namespace EEIRReg

enum class EEIROp : std::uint16_t
{
	// -- Meta / Flow -----------------------------------------------------------
	Nop = 0,
	Halt,               // End of block, no branch
	BlockEnd,           // End of block, branch target in imm
	Syscall,
	Break,
	Eret,               // COP0 exception return

	// -- Integer ALU (32-bit, result sign-extended to 64) ----------------------
	Add,                // dst = (s32)src0 + (s32)src1  (overflow trap)
	AddU,               // dst = (s32)src0 + (s32)src1  (no trap)
	Sub,                // dst = (s32)src0 - (s32)src1  (overflow trap)
	SubU,               // dst = (s32)src0 - (s32)src1  (no trap)
	AddImm,             // dst = (s32)src0 + (s16)imm   (overflow trap)
	AddImmU,            // dst = (s32)src0 + (s16)imm   (no trap)

	// -- Integer ALU (64-bit) --------------------------------------------------
	DAdd,               // dst = (s64)src0 + (s64)src1
	DAddU,
	DSub,
	DSubU,
	DAddImm,            // dst = (s64)src0 + (s16)imm
	DAddImmU,

	// -- Logical ---------------------------------------------------------------
	And,                // dst = src0 & src1
	Or,                 // dst = src0 | src1
	Xor,                // dst = src0 ^ src1
	Nor,                // dst = ~(src0 | src1)
	AndImm,             // dst = src0 & (u16)imm (zero-extended)
	OrImm,
	XorImm,

	// -- Set on Less Than ------------------------------------------------------
	Slt,                // dst = ((s32)src0 < (s32)src1) ? 1 : 0
	SltU,               // dst = ((u32)src0 < (u32)src1) ? 1 : 0
	SltImm,             // dst = ((s32)src0 < (s16)imm)  ? 1 : 0
	SltImmU,

	// -- Shifts (32-bit, result sign-extended) ---------------------------------
	Sll,                // dst = (s32)(src0 << imm[4:0])
	Srl,                // dst = (s32)((u32)src0 >> imm[4:0])
	Sra,                // dst = (s32)((s32)src0 >> imm[4:0])
	SllV,               // dst = (s32)(src0 << (src1 & 31))
	SrlV,
	SraV,

	// -- Shifts (64-bit) -------------------------------------------------------
	DSll,               // dst = src0 << imm[5:0]
	DSrl,
	DSra,
	DSll32,             // dst = src0 << (imm + 32)
	DSrl32,
	DSra32,
	DSllV,
	DSrlV,
	DSraV,

	// -- Multiply / Divide (write HI/LO) --------------------------------------
	Mult,               // (HI,LO) = (s32)src0 * (s32)src1; dst = LO
	MultU,
	Div,                // LO = (s32)src0 / (s32)src1; HI = remainder; dst = unused
	DivU,

	// -- Load Upper Immediate --------------------------------------------------
	Lui,                // dst = (s32)(imm << 16)

	// -- HI/LO moves -----------------------------------------------------------
	Mfhi,               // dst = HI
	Mthi,               // HI  = src0
	Mflo,               // dst = LO
	Mtlo,               // LO  = src0

	// -- SA moves (shift-amount register) --------------------------------------
	Mfsa,               // dst = SA
	Mtsa,               // SA  = src0
	Mtsab,              // SA  = ((src0 & 0xF) ^ (imm & 0xF)) << 3
	Mtsah,              // SA  = ((src0 & 0x7) ^ (imm & 0x7)) << 4

	// -- Conditional moves -----------------------------------------------------
	MovZ,               // if (src1 == 0) dst = src0
	MovN,               // if (src1 != 0) dst = src0

	// -- Load/Store (address = src0 + (s16)imm) --------------------------------
	LB,                 // dst = sign_ext8(mem[addr])
	LBU,                // dst = zero_ext8(mem[addr])
	LH,                 // dst = sign_ext16(mem[addr])
	LHU,
	LW,                 // dst = sign_ext32(mem[addr])
	LWU,                // dst = zero_ext32(mem[addr])
	LD,                 // dst = mem64[addr]
	LQ,                 // dst = mem128[addr] (quadword)
	LWL,                // load word left
	LWR,                // load word right
	LDL,                // load doubleword left
	LDR,                // load doubleword right

	SB,                 // mem8[addr]  = src1[7:0]    (src1 = value reg)
	SH,
	SW,
	SD,
	SQ,                 // mem128[addr] = src1
	SWL,
	SWR,
	SDL,
	SDR,

	// -- Branches (imm = branch offset in bytes from delay-slot PC) ------------
	// src0, src1 are the GPR indices being compared.
	Beq,
	Bne,
	Blez,
	Bgtz,
	Bltz,
	Bgez,

	// "Likely" variants — annul delay slot if branch not taken.
	BeqL,
	BneL,
	BlezL,
	BgtzL,
	BltzL,
	BgezL,

	// Link variants (store return address in r31 or dst).
	Bgezal,
	Bltzal,
	BgezalL,
	BltzalL,

	// -- Jumps (imm = 26-bit jump target field) --------------------------------
	J,                  // PC = (PC & 0xF000'0000) | (imm << 2)
	Jal,                // r31 = PC+8; jump
	Jr,                 // PC = src0
	Jalr,               // dst = PC+8; PC = src0

	// -- COP0 ------------------------------------------------------------------
	Mfc0,               // dst = COP0[src0]
	Mtc0,               // COP0[dst] = src0
	Tlbr,
	Tlbwi,
	Tlbwr,
	Tlbp,
	Di,                 // disable interrupts
	Ei,                 // enable interrupts

	// -- COP1 (FPU, single-precision) ------------------------------------------
	Mfc1,               // GPR dst = FPR src0 (bit-copy)
	Mtc1,               // FPR dst = GPR src0 (bit-copy)
	Cfc1,               // GPR dst = FCR[src0]
	Ctc1,               // FCR[dst] = GPR src0

	FAdd,               // FPR dst = FPR src0 + FPR src1
	FSub,
	FMul,
	FDiv,
	FSqrt,              // FPR dst = sqrt(FPR src0)
	FAbs,               // FPR dst = abs(FPR src0)
	FNeg,               // FPR dst = -FPR src0
	FMov,               // FPR dst = FPR src0

	CvtSW,              // FPR dst = (float)(s32)FPR src0
	CvtWS,              // FPR dst = (s32)FPR src0  (truncate)

	FCmpEQ,             // FPU condition = (src0 == src1)
	FCmpLT,             // FPU condition = (src0 <  src1)
	FCmpLE,             // FPU condition = (src0 <= src1)

	BC1F,               // branch if FPU condition false
	BC1T,               // branch if FPU condition true
	BC1FL,
	BC1TL,

	LWC1,               // FPR dst = mem32[src0 + imm]
	SWC1,               // mem32[src0 + imm] = FPR src1

	// -- FPU Accumulator ops ---------------------------------------------------
	FMadd,              // ACC += FPR src0 * FPR src1; FPR dst = ACC
	FMsub,              // ACC -= FPR src0 * FPR src1; FPR dst = ACC
	FMadda,             // ACC += FPR src0 * FPR src1
	FMsuba,             // ACC -= FPR src0 * FPR src1
	FAdda,              // ACC  = FPR src0 + FPR src1
	FSuba,              // ACC  = FPR src0 - FPR src1
	FMula,              // ACC  = FPR src0 * FPR src1
	FMax,               // FPR dst = max(src0, src1)
	FMin,               // FPR dst = min(src0, src1)
	FRsqrt,             // FPR dst = src0 / sqrt(src1)

	// -- System / Misc ---------------------------------------------------------
	Cache,              // hint only — nop in interpreter
	Pref,               // prefetch hint — nop
	Sync,               // memory barrier — nop in single-threaded

	// -- Traps -----------------------------------------------------------------
	Tge,
	TgeU,
	Tlt,
	TltU,
	Teq,
	Tne,
	TgeImm,
	TgeImmU,
	TltImm,
	TltImmU,
	TeqImm,
	TneImm,

	// -- COP2 (VU0 macro mode) placeholder — not expanded yet ------------------
	COP2,               // placeholder, decoded later

	// -- LQC2 / SQC2 (COP2 quadword load/store) -------------------------------
	LQC2,
	SQC2,

	// Keep this last.
	Count,
};

// Get a human-readable name for an IR opcode (for debug/logging).
const char* EEIROpName(EEIROp op);

// ============================================================================
// IR instruction node.
// ============================================================================
struct EEIRInstruction
{
	EEIROp op = EEIROp::Nop;
	std::uint8_t dst = EEIRReg::INVALID;   // destination virtual register
	std::uint8_t src0 = EEIRReg::INVALID;  // first source virtual register
	std::uint8_t src1 = EEIRReg::INVALID;  // second source virtual register
	std::int64_t imm = 0;                  // inline immediate / branch offset / jump target
};

// ============================================================================
// IR basic block — a straight-line sequence of IR nodes ending at a branch
// or jump.  The `start_pc` is the MIPS virtual address of the first decoded
// instruction.  `end_pc` is the address *after* the last decoded instruction
// (exclusive), including the delay slot when applicable.
// ============================================================================
struct EEIRBlock
{
	std::uint32_t start_pc = 0;
	std::uint32_t end_pc = 0;
	std::vector<EEIRInstruction> instructions;

	std::string Dump() const;
};
} // namespace armsx2::wasm
