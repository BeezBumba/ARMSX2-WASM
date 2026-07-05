#pragma once
#include "iop_hw_regs.h"
#include "iop_state.h"
#include "iop_ir.h"
#include <cstdint>
#include <string>

namespace armsx2::wasm
{
class IopIRInterpreter
{
public:
    struct RunResult
    {
        std::uint32_t next_pc = 0;
        std::uint32_t instructions_run = 0;
        bool halted = false;
        bool interrupt_pending = false;    // unmasked IOP interrupt raised
        std::string error;
    };

    // Execute one IR block.  On return, `state.pc` has NOT been updated — the
    // caller should set `state.pc = result.next_pc` to advance execution.
    // Pass a non-null `hw` to enable hardware register dispatch (I_STAT/I_MASK)
    // and interrupt checking at block boundaries.
    RunResult Execute(const IOPIRBlock& block, IOPState& state,
                      std::uint8_t* iop_memory, std::uint32_t iop_memory_size,
                      IOPHWRegs* hw = nullptr) const;

private:
    static std::uint8_t  ReadMem8 (const std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr);
    static std::uint16_t ReadMem16(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr);
    static std::uint32_t ReadMem32(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr);
    static void WriteMem8 (std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr, std::uint8_t  v);
    static void WriteMem16(std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr, std::uint16_t v);
    static void WriteMem32(std::uint8_t* mem, std::uint32_t sz, std::uint32_t addr, std::uint32_t v);
};
} // namespace armsx2::wasm
