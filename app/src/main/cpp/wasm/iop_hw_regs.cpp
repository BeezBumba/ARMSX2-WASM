#include "iop_hw_regs.h"

namespace armsx2::wasm
{

// Physical addresses of IOP interrupt-controller registers
static constexpr std::uint32_t PA_I_STAT = 0x1F801070u;
static constexpr std::uint32_t PA_I_MASK = 0x1F801074u;

// ============================================================================
// IOPHWRegs::Read32 — hardware register read (physical address)
// ============================================================================
std::uint32_t IOPHWRegs::Read32(std::uint32_t pa) const
{
    if (pa == PA_I_STAT) return i_stat;
    if (pa == PA_I_MASK) return i_mask;
    // All other IOP hardware registers: return 0 (bootstrap stub)
    return 0;
}

// ============================================================================
// IOPHWRegs::Write32 — hardware register write (physical address)
// ============================================================================
void IOPHWRegs::Write32(std::uint32_t pa, std::uint32_t val)
{
    // I_STAT: write-AND semantics — writing 0 to a bit acknowledges/clears it.
    // (Software typically writes: I_STAT & ~(1 << channel))
    if (pa == PA_I_STAT) { i_stat &= val; return; }
    // I_MASK: plain read/write.
    if (pa == PA_I_MASK) { i_mask = val; return; }
    // All other IOP hardware registers: silently ignore (bootstrap stub)
}

} // namespace armsx2::wasm
