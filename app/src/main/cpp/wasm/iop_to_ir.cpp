#include "iop_to_ir.h"

#include <cstring>
#include <sstream>

namespace armsx2::wasm
{
const char* IOPIROpName(IOPIROp op)
{
    switch (op)
    {
        case IOPIROp::Nop: return "Nop";
        case IOPIROp::Halt: return "Halt";
        case IOPIROp::BlockEnd: return "BlockEnd";
        case IOPIROp::Syscall: return "Syscall";
        case IOPIROp::Break: return "Break";
        case IOPIROp::Eret: return "Eret";
        case IOPIROp::Add: return "Add";
        case IOPIROp::AddU: return "AddU";
        case IOPIROp::Sub: return "Sub";
        case IOPIROp::SubU: return "SubU";
        case IOPIROp::AddImm: return "AddImm";
        case IOPIROp::AddImmU: return "AddImmU";
        case IOPIROp::And: return "And";
        case IOPIROp::Or: return "Or";
        case IOPIROp::Xor: return "Xor";
        case IOPIROp::Nor: return "Nor";
        case IOPIROp::AndImm: return "AndImm";
        case IOPIROp::OrImm: return "OrImm";
        case IOPIROp::XorImm: return "XorImm";
        case IOPIROp::Slt: return "Slt";
        case IOPIROp::SltU: return "SltU";
        case IOPIROp::SltImm: return "SltImm";
        case IOPIROp::SltImmU: return "SltImmU";
        case IOPIROp::Sll: return "Sll";
        case IOPIROp::Srl: return "Srl";
        case IOPIROp::Sra: return "Sra";
        case IOPIROp::SllV: return "SllV";
        case IOPIROp::SrlV: return "SrlV";
        case IOPIROp::SraV: return "SraV";
        case IOPIROp::Mult: return "Mult";
        case IOPIROp::MultU: return "MultU";
        case IOPIROp::Div: return "Div";
        case IOPIROp::DivU: return "DivU";
        case IOPIROp::Lui: return "Lui";
        case IOPIROp::Mfhi: return "Mfhi";
        case IOPIROp::Mthi: return "Mthi";
        case IOPIROp::Mflo: return "Mflo";
        case IOPIROp::Mtlo: return "Mtlo";
        case IOPIROp::MovZ: return "MovZ";
        case IOPIROp::MovN: return "MovN";
        case IOPIROp::LB: return "LB";
        case IOPIROp::LBU: return "LBU";
        case IOPIROp::LH: return "LH";
        case IOPIROp::LHU: return "LHU";
        case IOPIROp::LW: return "LW";
        case IOPIROp::LWL: return "LWL";
        case IOPIROp::LWR: return "LWR";
        case IOPIROp::SB: return "SB";
        case IOPIROp::SH: return "SH";
        case IOPIROp::SW: return "SW";
        case IOPIROp::SWL: return "SWL";
        case IOPIROp::SWR: return "SWR";
        case IOPIROp::Beq: return "Beq";
        case IOPIROp::Bne: return "Bne";
        case IOPIROp::Blez: return "Blez";
        case IOPIROp::Bgtz: return "Bgtz";
        case IOPIROp::Bltz: return "Bltz";
        case IOPIROp::Bgez: return "Bgez";
        case IOPIROp::BeqL: return "BeqL";
        case IOPIROp::BneL: return "BneL";
        case IOPIROp::BlezL: return "BlezL";
        case IOPIROp::BgtzL: return "BgtzL";
        case IOPIROp::BltzL: return "BltzL";
        case IOPIROp::BgezL: return "BgezL";
        case IOPIROp::Bgezal: return "Bgezal";
        case IOPIROp::Bltzal: return "Bltzal";
        case IOPIROp::J: return "J";
        case IOPIROp::Jal: return "Jal";
        case IOPIROp::Jr: return "Jr";
        case IOPIROp::Jalr: return "Jalr";
        case IOPIROp::Mfc0: return "Mfc0";
        case IOPIROp::Mtc0: return "Mtc0";
        case IOPIROp::Tlbr: return "Tlbr";
        case IOPIROp::Tlbwi: return "Tlbwi";
        case IOPIROp::Tlbwr: return "Tlbwr";
        case IOPIROp::Tlbp: return "Tlbp";
        case IOPIROp::COP2: return "COP2";
        case IOPIROp::LWC2: return "LWC2";
        case IOPIROp::SWC2: return "SWC2";
        case IOPIROp::Cache: return "Cache";
        case IOPIROp::Sync: return "Sync";
        default: return "???";
    }
}

std::string IOPIRBlock::Dump() const
{
    std::ostringstream out;
    out << "IOPIRBlock [" << std::hex << start_pc << ".." << end_pc << ") "
        << std::dec << instructions.size() << " IR nodes\n";
    for (size_t i = 0; i < instructions.size(); i++)
    {
        const auto& n = instructions[i];
        out << "  [" << i << "] " << IOPIROpName(n.op)
            << " dst=" << static_cast<int>(n.dst)
            << " src0=" << static_cast<int>(n.src0)
            << " src1=" << static_cast<int>(n.src1)
            << " imm=" << n.imm << "\n";
    }
    return out.str();
}

namespace
{
void Emit(IOPIRBlock& block, IOPIROp op, std::uint8_t dst = IOPIRReg::INVALID,
          std::uint8_t src0 = IOPIRReg::INVALID, std::uint8_t src1 = IOPIRReg::INVALID,
          std::int32_t imm = 0)
{
    block.instructions.push_back({op, dst, src0, src1, imm});
}

std::uint32_t ReadWord(const std::uint8_t* memory, std::uint32_t memory_size, std::uint32_t phys_addr)
{
    if (phys_addr + 4 > memory_size)
        return 0;
    std::uint32_t w = 0;
    std::memcpy(&w, memory + phys_addr, sizeof(w));
    return w;
}
} // namespace

IOPIRBlock IopLifter::LiftBlock(const std::uint8_t* memory, std::uint32_t memory_size,
                                std::uint32_t start_pc) const
{
    IOPIRBlock block;
    block.start_pc = start_pc;

    std::uint32_t pc = start_pc;
    bool in_delay_slot = false;
    bool block_done = false;
    constexpr std::uint32_t max_instructions = 512;

    for (std::uint32_t decoded = 0; !block_done && decoded < max_instructions; ++decoded)
    {
        const std::uint32_t phys = pc & 0x1FFFFFFF;
        const std::uint32_t op = ReadWord(memory, memory_size, phys);
        const bool is_branch = DecodeInstruction(op, pc, block);
        pc += 4;

        if (in_delay_slot)
            block_done = true;
        else if (is_branch)
            in_delay_slot = true;
    }

    if (!block_done)
        Emit(block, IOPIROp::BlockEnd, IOPIRReg::INVALID, IOPIRReg::INVALID, IOPIRReg::INVALID,
             static_cast<std::int32_t>(pc));

    block.end_pc = pc;
    return block;
}

bool IopLifter::DecodeInstruction(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const
{
    (void)pc;
    if (op == 0)
    {
        Emit(block, IOPIROp::Nop);
        return false;
    }

    switch (static_cast<std::uint8_t>((op >> 26) & 0x3F))
    {
        case 0x00: return DecodeSpecial(op, pc, block);
        case 0x01: return DecodeRegImm(op, pc, block);
        case 0x02: Emit(block, IOPIROp::J, IOPIRReg::INVALID, IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>(Target26(op))); return true;
        case 0x03: Emit(block, IOPIROp::Jal, IOPIRReg::GPR(31), IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>(Target26(op))); return true;
        case 0x04: Emit(block, IOPIROp::Beq, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x05: Emit(block, IOPIROp::Bne, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x06: Emit(block, IOPIROp::Blez, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x07: Emit(block, IOPIROp::Bgtz, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x08: Emit(block, IOPIROp::AddImm, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x09: Emit(block, IOPIROp::AddImmU, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x0A: Emit(block, IOPIROp::SltImm, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x0B: Emit(block, IOPIROp::SltImmU, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x0C: Emit(block, IOPIROp::AndImm, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16U(op))); return false;
        case 0x0D: Emit(block, IOPIROp::OrImm, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16U(op))); return false;
        case 0x0E: Emit(block, IOPIROp::XorImm, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16U(op))); return false;
        case 0x0F: Emit(block, IOPIROp::Lui, IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x10: return DecodeCOP0(op, pc, block);
        case 0x12: Emit(block, IOPIROp::COP2, IOPIRReg::INVALID, IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>(op)); return false;
        case 0x14: Emit(block, IOPIROp::BeqL, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x15: Emit(block, IOPIROp::BneL, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x16: Emit(block, IOPIROp::BlezL, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x17: Emit(block, IOPIROp::BgtzL, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x20: Emit(block, IOPIROp::LB, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x21: Emit(block, IOPIROp::LH, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x22: Emit(block, IOPIROp::LWL, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x23: Emit(block, IOPIROp::LW, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x24: Emit(block, IOPIROp::LBU, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x25: Emit(block, IOPIROp::LHU, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x26: Emit(block, IOPIROp::LWR, IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x28: Emit(block, IOPIROp::SB, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x29: Emit(block, IOPIROp::SH, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x2A: Emit(block, IOPIROp::SWL, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x2B: Emit(block, IOPIROp::SW, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x2E: Emit(block, IOPIROp::SWR, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x2F: Emit(block, IOPIROp::Cache, IOPIRReg::INVALID, IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>(op)); return false;
        case 0x32: Emit(block, IOPIROp::LWC2, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        case 0x3A: Emit(block, IOPIROp::SWC2, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op))); return false;
        default: Emit(block, IOPIROp::Nop); return false;
    }
}

bool IopLifter::DecodeSpecial(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const
{
    (void)pc;
    switch (Funct(op))
    {
        case 0x00: Emit(block, IOPIROp::Sll, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, Shamt(op)); return false;
        case 0x02: Emit(block, IOPIROp::Srl, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, Shamt(op)); return false;
        case 0x03: Emit(block, IOPIROp::Sra, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::INVALID, Shamt(op)); return false;
        case 0x04: Emit(block, IOPIROp::SllV, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op))); return false;
        case 0x06: Emit(block, IOPIROp::SrlV, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op))); return false;
        case 0x07: Emit(block, IOPIROp::SraV, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rt(op)), IOPIRReg::GPR(Rs(op))); return false;
        case 0x08: Emit(block, IOPIROp::Jr, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op))); return true;
        case 0x09: Emit(block, IOPIROp::Jalr, IOPIRReg::GPR(Rd(op) ? Rd(op) : 31), IOPIRReg::GPR(Rs(op))); return true;
        case 0x0A: Emit(block, IOPIROp::MovZ, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x0B: Emit(block, IOPIROp::MovN, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x0C: Emit(block, IOPIROp::Syscall, IOPIRReg::INVALID, IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>((op >> 6) & 0xFFFFF)); return true;
        case 0x0D: Emit(block, IOPIROp::Break, IOPIRReg::INVALID, IOPIRReg::INVALID, IOPIRReg::INVALID, static_cast<std::int32_t>((op >> 6) & 0xFFFFF)); return true;
        case 0x10: Emit(block, IOPIROp::Mfhi, IOPIRReg::GPR(Rd(op))); return false;
        case 0x11: Emit(block, IOPIROp::Mthi, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op))); return false;
        case 0x12: Emit(block, IOPIROp::Mflo, IOPIRReg::GPR(Rd(op))); return false;
        case 0x13: Emit(block, IOPIROp::Mtlo, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op))); return false;
        case 0x18: Emit(block, IOPIROp::Mult, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x19: Emit(block, IOPIROp::MultU, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x1A: Emit(block, IOPIROp::Div, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x1B: Emit(block, IOPIROp::DivU, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x20: Emit(block, IOPIROp::Add, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x21: Emit(block, IOPIROp::AddU, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x22: Emit(block, IOPIROp::Sub, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x23: Emit(block, IOPIROp::SubU, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x24: Emit(block, IOPIROp::And, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x25: Emit(block, IOPIROp::Or, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x26: Emit(block, IOPIROp::Xor, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x27: Emit(block, IOPIROp::Nor, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x2A: Emit(block, IOPIROp::Slt, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x2B: Emit(block, IOPIROp::SltU, IOPIRReg::GPR(Rd(op)), IOPIRReg::GPR(Rs(op)), IOPIRReg::GPR(Rt(op))); return false;
        default: Emit(block, IOPIROp::Nop); return false;
    }
}

bool IopLifter::DecodeRegImm(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const
{
    (void)pc;
    switch (Rt(op))
    {
        case 0x00: Emit(block, IOPIROp::Bltz, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x01: Emit(block, IOPIROp::Bgez, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x02: Emit(block, IOPIROp::BltzL, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x03: Emit(block, IOPIROp::BgezL, IOPIRReg::INVALID, IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x10: Emit(block, IOPIROp::Bltzal, IOPIRReg::GPR(31), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        case 0x11: Emit(block, IOPIROp::Bgezal, IOPIRReg::GPR(31), IOPIRReg::GPR(Rs(op)), IOPIRReg::INVALID, static_cast<std::int32_t>(Imm16(op) * 4)); return true;
        default: Emit(block, IOPIROp::Nop); return false;
    }
}

bool IopLifter::DecodeCOP0(std::uint32_t op, std::uint32_t pc, IOPIRBlock& block) const
{
    (void)pc;
    switch (Rs(op))
    {
        case 0x00: Emit(block, IOPIROp::Mfc0, IOPIRReg::GPR(Rt(op)), IOPIRReg::COP0(Rd(op))); return false;
        case 0x04: Emit(block, IOPIROp::Mtc0, IOPIRReg::COP0(Rd(op)), IOPIRReg::GPR(Rt(op))); return false;
        case 0x10:
            switch (Funct(op))
            {
                case 0x01: Emit(block, IOPIROp::Tlbr); return false;
                case 0x02: Emit(block, IOPIROp::Tlbwi); return false;
                case 0x06: Emit(block, IOPIROp::Tlbwr); return false;
                case 0x08: Emit(block, IOPIROp::Tlbp); return false;
                case 0x18: Emit(block, IOPIROp::Eret); return true;
                default: Emit(block, IOPIROp::Nop); return false;
            }
        default:
            Emit(block, IOPIROp::Nop);
            return false;
    }
}
} // namespace armsx2::wasm
