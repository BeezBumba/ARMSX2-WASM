#pragma once
#include "iop_ir.h"
#include <cstdint>

namespace armsx2::wasm
{
class IopLifter
{
public:
    IOPIRBlock LiftBlock(const std::uint8_t* memory, std::uint32_t memory_size,
                         std::uint32_t start_pc) const;

private:
    static std::uint8_t  Rs(std::uint32_t op)    { return static_cast<std::uint8_t>((op >> 21) & 0x1F); }
    static std::uint8_t  Rt(std::uint32_t op)    { return static_cast<std::uint8_t>((op >> 16) & 0x1F); }
    static std::uint8_t  Rd(std::uint32_t op)    { return static_cast<std::uint8_t>((op >> 11) & 0x1F); }
    static std::uint8_t  Shamt(std::uint32_t op) { return static_cast<std::uint8_t>((op >>  6) & 0x1F); }
    static std::uint8_t  Funct(std::uint32_t op) { return static_cast<std::uint8_t>(op & 0x3F); }
    static std::int16_t  Imm16(std::uint32_t op) { return static_cast<std::int16_t>(op & 0xFFFF); }
    static std::uint16_t Imm16U(std::uint32_t op){ return static_cast<std::uint16_t>(op & 0xFFFF); }
    static std::uint32_t Target26(std::uint32_t op) { return op & 0x03FFFFFF; }

    bool DecodeInstruction(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const;
    bool DecodeSpecial(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const;
    bool DecodeRegImm(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const;
    bool DecodeCOP0(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const;
};
} // namespace armsx2::wasm
