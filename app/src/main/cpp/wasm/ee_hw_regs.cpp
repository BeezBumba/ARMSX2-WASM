#include "ee_hw_regs.h"

namespace armsx2::wasm
{

// Physical addresses of INTC registers
static constexpr std::uint32_t PA_INTC_STAT = 0x1000F000u;
static constexpr std::uint32_t PA_INTC_MASK = 0x1000F010u;
static constexpr std::uint32_t PA_DMAC_CTRL = 0x1000E000u;
static constexpr std::uint32_t PA_DMAC_STAT = 0x1000E010u;
static constexpr std::uint32_t PA_DMAC_PCR  = 0x1000E020u;
static constexpr std::uint32_t PA_SIF_MSFLAG = 0x1000F220u;
static constexpr std::uint32_t PA_SIF_SMFLAG = 0x1000F230u;

// Physical base addresses for the four timers (stride 0x800)
static constexpr std::uint32_t PA_RCNT_BASE[4] = {
    0x10000000u, 0x10000800u, 0x10001000u, 0x10001800u
};

// ============================================================================
// EERcnt::Tick — advance one timer and return flags for INTC raising
// ============================================================================
std::uint32_t EERcnt::Tick(std::uint32_t ticks)
{
    if (!(mode & EERcntMode::CUE))
        return 0; // timer disabled

    std::uint32_t flags = 0;
    const std::uint32_t prev = count;
    count = (count + ticks) & 0xFFFF;

    // Compare match
    if ((mode & EERcntMode::CMPE) && comp != 0 && prev < comp && count >= comp)
    {
        mode |= EERcntMode::EQUF;
        flags |= 1u;
        if (mode & EERcntMode::ZRET)
            count = 0;
    }

    // Overflow (16-bit wrap)
    if ((mode & EERcntMode::OVFE) && count < prev)
    {
        mode |= EERcntMode::OVFF;
        flags |= 2u;
    }

    return flags;
}

// ============================================================================
// EEHWRegs::TickTimers — advance all enabled timers
// ============================================================================
void EEHWRegs::TickTimers(std::uint32_t bus_cycles)
{
    for (int i = 0; i < 4; i++)
    {
        const std::uint32_t flags = rcnt[i].Tick(bus_cycles);
        if (flags & 1u)
            intc_stat |= (1u << (EEINTCBit::TIM0 + static_cast<std::uint32_t>(i)));
        if (flags & 2u)
            intc_stat |= (1u << (EEINTCBit::TIM0 + static_cast<std::uint32_t>(i)));
    }
}

// ============================================================================
// EEHWRegs::Read32 — hardware register read
// ============================================================================
std::uint32_t EEHWRegs::Read32(std::uint32_t pa) const
{
    // INTC
    if (pa == PA_INTC_STAT) return intc_stat;
    if (pa == PA_INTC_MASK) return intc_mask;
    if (pa == PA_DMAC_CTRL) return dmac_ctrl;
    if (pa == PA_DMAC_STAT) return dmac_stat;
    if (pa == PA_DMAC_PCR)  return dmac_pcr;
    if (pa == PA_SIF_MSFLAG) return sif_msflag;
    if (pa == PA_SIF_SMFLAG) return sif_smflag;

    // Timers (RCNT0–3, each occupies 0x40 bytes starting at its base)
    for (int i = 0; i < 4; i++)
    {
        if (pa >= PA_RCNT_BASE[i] && pa < PA_RCNT_BASE[i] + 0x40u)
        {
            switch (pa - PA_RCNT_BASE[i])
            {
                case 0x00: return rcnt[i].count;
                case 0x10: return rcnt[i].mode;
                case 0x20: return rcnt[i].comp;
                case 0x30: return rcnt[i].hold; // only meaningful for RCNT0/1
                default:   return 0;
            }
        }
    }

    // All other HW registers (GIF, VIF, DMAC, etc.) — return 0 for now
    return 0;
}

// ============================================================================
// EEHWRegs::Write32 — hardware register write
// ============================================================================
void EEHWRegs::Write32(std::uint32_t pa, std::uint32_t val)
{
    // INTC_STAT: writing a 1 to a bit CLEARS it (write-XOR semantics on PS2)
    if (pa == PA_INTC_STAT) { intc_stat &= ~val; return; }
    // INTC_MASK: writing a 1 TOGGLES the corresponding enable bit
    if (pa == PA_INTC_MASK) { intc_mask ^= val;  return; }
    if (pa == PA_DMAC_CTRL) { dmac_ctrl = val; return; }
    if (pa == PA_DMAC_STAT) { dmac_stat = val; return; }
    if (pa == PA_DMAC_PCR)  { dmac_pcr = val; return; }
    if (pa == PA_SIF_MSFLAG) { sif_msflag = val; return; }
    if (pa == PA_SIF_SMFLAG) { sif_smflag = val; return; }

    // Timers
    for (int i = 0; i < 4; i++)
    {
        if (pa >= PA_RCNT_BASE[i] && pa < PA_RCNT_BASE[i] + 0x40u)
        {
            switch (pa - PA_RCNT_BASE[i])
            {
                case 0x00: rcnt[i].count = val & 0xFFFF; return;
                case 0x10:
                    // Writing to T_MODE clears the EQUF/OVFF sticky bits
                    rcnt[i].mode = (val & ~(EERcntMode::EQUF | EERcntMode::OVFF));
                    return;
                case 0x20: rcnt[i].comp = val & 0xFFFF; return;
                case 0x30: rcnt[i].hold = val & 0xFFFF; return;
                default:   return;
            }
        }
    }

    // Other HW registers: silently ignored (stubs will be filled as needed)
}

} // namespace armsx2::wasm
