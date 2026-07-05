#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace armsx2::wasm
{
namespace IOPIRReg
{
    constexpr std::uint8_t GPR(unsigned i) { return static_cast<std::uint8_t>(i & 31); }
    constexpr std::uint8_t HI  = 32;
    constexpr std::uint8_t LO  = 33;
    constexpr std::uint8_t COP0(unsigned i) { return static_cast<std::uint8_t>(64 + (i & 31)); }
    constexpr std::uint8_t INVALID = 255;
} // namespace IOPIRReg

enum class IOPIROp : std::uint8_t
{
    Nop = 0,
    Halt,
    BlockEnd,
    Syscall,
    Break,
    Eret,

    Add, AddU, Sub, SubU,
    AddImm, AddImmU,

    And, Or, Xor, Nor,
    AndImm, OrImm, XorImm,

    Slt, SltU, SltImm, SltImmU,

    Sll, Srl, Sra,
    SllV, SrlV, SraV,

    Mult, MultU, Div, DivU,

    Lui,

    Mfhi, Mthi, Mflo, Mtlo,

    MovZ, MovN,

    LB, LBU, LH, LHU, LW,
    LWL, LWR,

    SB, SH, SW,
    SWL, SWR,

    Beq, Bne, Blez, Bgtz,
    Bltz, Bgez,
    BeqL, BneL, BlezL, BgtzL,
    BltzL, BgezL,
    Bgezal, Bltzal,

    J, Jal, Jr, Jalr,

    Mfc0, Mtc0,
    Tlbr, Tlbwi, Tlbwr, Tlbp,

    COP2,
    LWC2,
    SWC2,

    Cache, Sync,

    Count,
};

const char* IOPIROpName(IOPIROp op);

struct IOPIRInstruction
{
    IOPIROp op = IOPIROp::Nop;
    std::uint8_t dst   = IOPIRReg::INVALID;
    std::uint8_t src0  = IOPIRReg::INVALID;
    std::uint8_t src1  = IOPIRReg::INVALID;
    std::int32_t imm   = 0;
};

struct IOPIRBlock
{
    std::uint32_t start_pc = 0;
    std::uint32_t end_pc   = 0;
    std::vector<IOPIRInstruction> instructions;

    std::string Dump() const;
};
} // namespace armsx2::wasm
