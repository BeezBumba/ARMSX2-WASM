#include "iop_ir_interpreter.h"

#include <cstring>
#include <limits>

namespace armsx2::wasm
{
namespace
{
std::uint32_t ToPhys(std::uint32_t vaddr)
{
    return vaddr & 0x1FFFFFFF;
}

// IOP hardware register range: 0x1F800000–0x1F803FFF
// (interrupt controller, DMA, timers, and other I/O ports)
constexpr std::uint32_t IOP_HW_BASE = 0x1F800000u;
constexpr std::uint32_t IOP_HW_END  = 0x1F804000u;

inline bool IsIOPHWReg(std::uint32_t pa)
{
    return pa >= IOP_HW_BASE && pa < IOP_HW_END;
}
} // namespace

std::uint8_t IopIRInterpreter::ReadMem8(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr)
{
    const std::uint32_t pa = ToPhys(addr);
    return (pa < sz) ? mem[pa] : 0;
}

std::uint16_t IopIRInterpreter::ReadMem16(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr)
{
    const std::uint32_t pa = ToPhys(addr);
    if (pa + 2 > sz) return 0;
    std::uint16_t v = 0;
    std::memcpy(&v, mem + pa, sizeof(v));
    return v;
}

std::uint32_t IopIRInterpreter::ReadMem32(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr)
{
    const std::uint32_t pa = ToPhys(addr);
    if (pa + 4 > sz) return 0;
    std::uint32_t v = 0;
    std::memcpy(&v, mem + pa, sizeof(v));
    return v;
}

void IopIRInterpreter::WriteMem8(std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr, std::uint8_t v)
{
    const std::uint32_t pa = ToPhys(addr);
    if (pa < sz) mem[pa] = v;
}

void IopIRInterpreter::WriteMem16(std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr, std::uint16_t v)
{
    const std::uint32_t pa = ToPhys(addr);
    if (pa + 2 <= sz) std::memcpy(mem + pa, &v, sizeof(v));
}

void IopIRInterpreter::WriteMem32(std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr, std::uint32_t v)
{
    const std::uint32_t pa = ToPhys(addr);
    if (pa + 4 <= sz) std::memcpy(mem + pa, &v, sizeof(v));
}

IopIRInterpreter::RunResult IopIRInterpreter::Execute(const IOPIRBlock& block, IOPState& state,
                                                      std::uint8_t* iop_memory, std::uint32_t iop_memory_size,
                                                      IOPHWRegs* hw) const
{
    RunResult result;
    result.next_pc = block.end_pc;

    // -------------------------------------------------------------------------
    // Hardware-aware memory access lambdas.
    // Reads/writes to the IOP I/O region (0x1F800000–0x1F803FFF) are routed to
    // the IOPHWRegs state (I_STAT, I_MASK, etc.) instead of flat RAM.
    // -------------------------------------------------------------------------
    auto read32 = [&](std::uint32_t addr) -> std::uint32_t {
        const std::uint32_t pa = ToPhys(addr);
        if (hw && IsIOPHWReg(pa)) return hw->Read32(pa);
        return ReadMem32(iop_memory, iop_memory_size, addr);
    };
    auto write32 = [&](std::uint32_t addr, std::uint32_t val) {
        const std::uint32_t pa = ToPhys(addr);
        if (hw && IsIOPHWReg(pa)) { hw->Write32(pa, val); return; }
        WriteMem32(iop_memory, iop_memory_size, addr, val);
    };

    bool branch_taken = false;
    std::uint32_t branch_target = 0;

    for (size_t i = 0; i < block.instructions.size(); i++)
    {
        const auto& n = block.instructions[i];
        result.instructions_run++;

        switch (n.op)
        {
            case IOPIROp::Nop:
            case IOPIROp::Cache:
            case IOPIROp::Sync:
            case IOPIROp::Tlbr:
            case IOPIROp::Tlbwi:
            case IOPIROp::Tlbwr:
            case IOPIROp::Tlbp:
            case IOPIROp::COP2:
            case IOPIROp::LWC2:
            case IOPIROp::SWC2:
                break;

            case IOPIROp::Halt:
                result.halted = true;
                return result;

            case IOPIROp::BlockEnd:
                result.next_pc = static_cast<std::uint32_t>(n.imm);
                return result;

            case IOPIROp::Syscall:
                state.cop0[14] = block.end_pc - 8;
                state.cop0[13] = (state.cop0[13] & ~0xFFu) | (8u << 2);
                result.next_pc = (state.cop0[12] & (1u << 22)) ? 0xBFC00180u : 0x80u;
                return result;

            case IOPIROp::Break:
                result.halted = true;
                result.error = "BREAK encountered";
                return result;

            case IOPIROp::Eret:
                result.next_pc = state.cop0[14];
                return result;

            case IOPIROp::Add:
            case IOPIROp::AddU:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(state.ReadGPR(n.src0) + state.ReadGPR(n.src1)));
                break;

            case IOPIROp::Sub:
            case IOPIROp::SubU:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(state.ReadGPR(n.src0) - state.ReadGPR(n.src1)));
                break;

            case IOPIROp::AddImm:
            case IOPIROp::AddImmU:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm)));
                break;

            case IOPIROp::And:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) & state.ReadGPR(n.src1));
                break;
            case IOPIROp::Or:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) | state.ReadGPR(n.src1));
                break;
            case IOPIROp::Xor:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) ^ state.ReadGPR(n.src1));
                break;
            case IOPIROp::Nor:
                state.WriteGPR(n.dst, ~(state.ReadGPR(n.src0) | state.ReadGPR(n.src1)));
                break;
            case IOPIROp::AndImm:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) & static_cast<std::uint16_t>(n.imm));
                break;
            case IOPIROp::OrImm:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) | static_cast<std::uint16_t>(n.imm));
                break;
            case IOPIROp::XorImm:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) ^ static_cast<std::uint16_t>(n.imm));
                break;

            case IOPIROp::Slt:
                state.WriteGPR(n.dst, (static_cast<std::int32_t>(state.ReadGPR(n.src0)) < static_cast<std::int32_t>(state.ReadGPR(n.src1))) ? 1u : 0u);
                break;
            case IOPIROp::SltU:
                state.WriteGPR(n.dst, (state.ReadGPR(n.src0) < state.ReadGPR(n.src1)) ? 1u : 0u);
                break;
            case IOPIROp::SltImm:
                state.WriteGPR(n.dst, (static_cast<std::int32_t>(state.ReadGPR(n.src0)) < static_cast<std::int16_t>(n.imm)) ? 1u : 0u);
                break;
            case IOPIROp::SltImmU:
                state.WriteGPR(n.dst, (state.ReadGPR(n.src0) < static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(n.imm)))) ? 1u : 0u);
                break;

            case IOPIROp::Sll:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) << (n.imm & 31));
                break;
            case IOPIROp::Srl:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) >> (n.imm & 31));
                break;
            case IOPIROp::Sra:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(static_cast<std::int32_t>(state.ReadGPR(n.src0)) >> (n.imm & 31)));
                break;
            case IOPIROp::SllV:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) << (state.ReadGPR(n.src1) & 31));
                break;
            case IOPIROp::SrlV:
                state.WriteGPR(n.dst, state.ReadGPR(n.src0) >> (state.ReadGPR(n.src1) & 31));
                break;
            case IOPIROp::SraV:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(static_cast<std::int32_t>(state.ReadGPR(n.src0)) >> (state.ReadGPR(n.src1) & 31)));
                break;

            case IOPIROp::Mult:
            {
                const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(state.ReadGPR(n.src0))) *
                                             static_cast<std::int64_t>(static_cast<std::int32_t>(state.ReadGPR(n.src1)));
                state.lo = static_cast<std::uint32_t>(product);
                state.hi = static_cast<std::uint32_t>(product >> 32);
                break;
            }

            case IOPIROp::MultU:
            {
                const std::uint64_t product = static_cast<std::uint64_t>(state.ReadGPR(n.src0)) *
                                              static_cast<std::uint64_t>(state.ReadGPR(n.src1));
                state.lo = static_cast<std::uint32_t>(product);
                state.hi = static_cast<std::uint32_t>(product >> 32);
                break;
            }

            case IOPIROp::Div:
            {
                const std::int32_t a = static_cast<std::int32_t>(state.ReadGPR(n.src0));
                const std::int32_t b = static_cast<std::int32_t>(state.ReadGPR(n.src1));
                if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
                {
                    state.lo = 0x80000000u;
                    state.hi = 0;
                }
                else if (b != 0)
                {
                    state.lo = static_cast<std::uint32_t>(a / b);
                    state.hi = static_cast<std::uint32_t>(a % b);
                }
                else
                {
                    state.lo = static_cast<std::uint32_t>((a < 0) ? 1 : -1);
                    state.hi = static_cast<std::uint32_t>(a);
                }
                break;
            }

            case IOPIROp::DivU:
            {
                const std::uint32_t a = state.ReadGPR(n.src0);
                const std::uint32_t b = state.ReadGPR(n.src1);
                if (b != 0)
                {
                    state.lo = a / b;
                    state.hi = a % b;
                }
                else
                {
                    state.lo = 0xFFFFFFFFu;
                    state.hi = a;
                }
                break;
            }

            case IOPIROp::Lui:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(n.imm)) << 16));
                break;

            case IOPIROp::Mfhi:
                state.WriteGPR(n.dst, state.hi);
                break;
            case IOPIROp::Mthi:
                state.hi = state.ReadGPR(n.src0);
                break;
            case IOPIROp::Mflo:
                state.WriteGPR(n.dst, state.lo);
                break;
            case IOPIROp::Mtlo:
                state.lo = state.ReadGPR(n.src0);
                break;

            case IOPIROp::MovZ:
                if (state.ReadGPR(n.src1) == 0) state.WriteGPR(n.dst, state.ReadGPR(n.src0));
                break;
            case IOPIROp::MovN:
                if (state.ReadGPR(n.src1) != 0) state.WriteGPR(n.dst, state.ReadGPR(n.src0));
                break;

            case IOPIROp::LB:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(ReadMem8(iop_memory, iop_memory_size, state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm))))));
                break;
            case IOPIROp::LBU:
                state.WriteGPR(n.dst, ReadMem8(iop_memory, iop_memory_size, state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm)));
                break;
            case IOPIROp::LH:
                state.WriteGPR(n.dst, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(ReadMem16(iop_memory, iop_memory_size, state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm))))));
                break;
            case IOPIROp::LHU:
                state.WriteGPR(n.dst, ReadMem16(iop_memory, iop_memory_size, state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm)));
                break;
            case IOPIROp::LW:
                state.WriteGPR(n.dst, read32(state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm)));
                break;
            case IOPIROp::LWL:
            case IOPIROp::LWR:
                state.WriteGPR(n.dst, read32((state.ReadGPR(n.src0) + static_cast<std::int16_t>(n.imm)) & ~3u));
                break;

            case IOPIROp::SB:
                WriteMem8(iop_memory, iop_memory_size, state.ReadGPR(n.dst) + static_cast<std::int16_t>(n.imm), static_cast<std::uint8_t>(state.ReadGPR(n.src0)));
                break;
            case IOPIROp::SH:
                WriteMem16(iop_memory, iop_memory_size, state.ReadGPR(n.dst) + static_cast<std::int16_t>(n.imm), static_cast<std::uint16_t>(state.ReadGPR(n.src0)));
                break;
            case IOPIROp::SW:
                write32(state.ReadGPR(n.dst) + static_cast<std::int16_t>(n.imm), state.ReadGPR(n.src0));
                break;
            case IOPIROp::SWL:
            case IOPIROp::SWR:
                write32((state.ReadGPR(n.dst) + static_cast<std::int16_t>(n.imm)) & ~3u, state.ReadGPR(n.src0));
                break;

            case IOPIROp::Beq:
            case IOPIROp::BeqL:
                if (state.ReadGPR(n.src0) == state.ReadGPR(n.src1))
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Bne:
            case IOPIROp::BneL:
                if (state.ReadGPR(n.src0) != state.ReadGPR(n.src1))
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Blez:
            case IOPIROp::BlezL:
                if (static_cast<std::int32_t>(state.ReadGPR(n.src0)) <= 0)
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Bgtz:
            case IOPIROp::BgtzL:
                if (static_cast<std::int32_t>(state.ReadGPR(n.src0)) > 0)
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Bltz:
            case IOPIROp::BltzL:
                if (static_cast<std::int32_t>(state.ReadGPR(n.src0)) < 0)
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Bgez:
            case IOPIROp::BgezL:
                if (static_cast<std::int32_t>(state.ReadGPR(n.src0)) >= 0)
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Bgezal:
                state.WriteGPR(n.dst, block.end_pc);
                if (static_cast<std::int32_t>(state.ReadGPR(n.src0)) >= 0)
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;
            case IOPIROp::Bltzal:
                state.WriteGPR(n.dst, block.end_pc);
                if (static_cast<std::int32_t>(state.ReadGPR(n.src0)) < 0)
                {
                    branch_taken = true;
                    branch_target = static_cast<std::uint32_t>(static_cast<std::int64_t>(block.end_pc - 4) + n.imm);
                }
                break;

            case IOPIROp::J:
                branch_taken = true;
                branch_target = (block.start_pc & 0xF0000000u) | (static_cast<std::uint32_t>(n.imm) << 2);
                break;
            case IOPIROp::Jal:
                state.WriteGPR(n.dst, block.end_pc);
                branch_taken = true;
                branch_target = (block.start_pc & 0xF0000000u) | (static_cast<std::uint32_t>(n.imm) << 2);
                break;
            case IOPIROp::Jr:
                branch_taken = true;
                branch_target = state.ReadGPR(n.src0);
                break;
            case IOPIROp::Jalr:
                state.WriteGPR(n.dst, block.end_pc);
                branch_taken = true;
                branch_target = state.ReadGPR(n.src0);
                break;

            case IOPIROp::Mfc0:
                state.WriteGPR(n.dst, state.cop0[n.src0 - IOPIRReg::COP0(0)]);
                break;
            case IOPIROp::Mtc0:
                state.cop0[n.dst - IOPIRReg::COP0(0)] = state.ReadGPR(n.src0);
                break;

            case IOPIROp::Rfe:
                // R3000A RFE: restore the mode/interrupt stack in COP0 Status (reg 12).
                // Shifts [IEp, KUp] → [IEc, KUc] and [IEo, KUo] → [IEp, KUp].
                // This does NOT change PC; the preceding JR $k0 provides the return address.
                state.cop0[12] = (state.cop0[12] & ~0x0Fu) | ((state.cop0[12] >> 2) & 0x0Fu);
                break;

            default:
                break;
        }
    }

    if (branch_taken)
        result.next_pc = branch_target;

    // Check for pending IOP interrupts at the block boundary.
    // An interrupt is pending when (I_STAT & I_MASK) != 0 and the IOP COP0
    // Status IEc bit (bit 0) is set.  We surface the flag to the caller to
    // decide whether to vector to the exception handler (0x80000080).
    // (On exception entry the hardware clears IEc to prevent re-entry, so
    // the flag will not re-fire while the handler is running.)
    if (hw && hw->AnyPending())
    {
        const bool iec = (state.cop0[12] & 1u) != 0;  // Status[0]: IEc (current interrupt enable)
        if (iec)
            result.interrupt_pending = true;
    }

    return result;
}
} // namespace armsx2::wasm
