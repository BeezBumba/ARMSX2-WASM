#pragma once

#include <cstdint>

// ============================================================================
// EE hardware register state — minimal implementation covering the subsystems
// that games poll most frequently: INTC and the four hardware timers (RCNT).
//
// Physical address map (after addr & 0x1FFFFFFF):
//   0x10000000–0x10001FFF : Timers RCNT0–3 (stride 0x800)
//   0x10002000–0x1000EFFF : Other EE HW (GIF/VIF/DMAC stubs – returns 0)
//   0x1000F000             : INTC_STAT  (write-XOR clears bits)
//   0x1000F010             : INTC_MASK  (write-XOR toggles mask bits)
// ============================================================================

namespace armsx2::wasm
{

// INTC interrupt bit indices (EE Interrupt Controller)
namespace EEINTCBit
{
    constexpr std::uint32_t GS      =  0;  // Graphics Synthesizer
    constexpr std::uint32_t SBUS    =  1;  // Sub-bus (IOP side)
    constexpr std::uint32_t VBLANKs =  2;  // VBlank start
    constexpr std::uint32_t VBLANKe =  3;  // VBlank end
    constexpr std::uint32_t VIF0    =  4;
    constexpr std::uint32_t VIF1    =  5;
    constexpr std::uint32_t VU0     =  6;
    constexpr std::uint32_t VU1     =  7;
    constexpr std::uint32_t IPU     =  8;
    constexpr std::uint32_t TIM0    =  9;
    constexpr std::uint32_t TIM1    = 10;
    constexpr std::uint32_t TIM2    = 11;
    constexpr std::uint32_t TIM3    = 12;
    constexpr std::uint32_t SFIFO   = 13;
    constexpr std::uint32_t VU0WD   = 14;
} // namespace EEINTCBit

// Timer mode register bits (T_MODE)
namespace EERcntMode
{
    constexpr std::uint32_t CLKS   = 0x0003; // clock source (bits 1:0)
    constexpr std::uint32_t GATE   = 0x0004; // gate enable
    constexpr std::uint32_t GATS   = 0x0008; // gate select
    constexpr std::uint32_t GATM   = 0x0030; // gate mode (bits 5:4)
    constexpr std::uint32_t ZRET   = 0x0040; // zero return on compare
    constexpr std::uint32_t CUE    = 0x0080; // count-up enable
    constexpr std::uint32_t CMPE   = 0x0100; // compare interrupt enable
    constexpr std::uint32_t OVFE   = 0x0200; // overflow interrupt enable
    constexpr std::uint32_t EQUF   = 0x0400; // equal flag (compare match)
    constexpr std::uint32_t OVFF   = 0x0800; // overflow flag
} // namespace EERcntMode

// One EE hardware timer (RCNT)
struct EERcnt
{
    std::uint32_t count = 0;   // T_COUNT (16-bit counter)
    std::uint32_t mode  = 0;   // T_MODE
    std::uint32_t comp  = 0;   // T_COMP (compare target)
    std::uint32_t hold  = 0;   // T_HOLD (RCNT0/1 only)

    // Advance the counter by `ticks` bus-clock cycles.
    // Raises INTC TIMx interrupt bits through the returned flag word:
    //   bit 0 = compare match (raised EQUF), bit 1 = overflow (raised OVFF).
    std::uint32_t Tick(std::uint32_t ticks);
};

// EE hardware register state (INTC + 4 timers)
struct EEHWRegs
{
    std::uint32_t intc_stat = 0;    // INTC_STAT: pending interrupt bits
    std::uint32_t intc_mask = 0;    // INTC_MASK: enabled interrupt bits
    std::uint32_t dmac_ctrl = 0;    // DMAC control (bootstrap stub)
    std::uint32_t dmac_stat = 0;    // DMAC status (bootstrap stub)
    std::uint32_t dmac_pcr  = 0;    // DMAC priority control (bootstrap stub)
    std::uint32_t sif_msflag = 0;   // SIF EE->IOP flag (bootstrap stub)
    std::uint32_t sif_smflag = 0;   // SIF IOP->EE flag (bootstrap stub)
    EERcnt        rcnt[4];          // RCNT0–RCNT3

    // --- VBlank helpers ------------------------------------------------------
    // Call at the start/end of each simulated vertical blank period (~60 Hz).
    void RaiseVBlankStart() { intc_stat |= (1u << EEINTCBit::VBLANKs); }
    void RaiseVBlankEnd()   { intc_stat |= (1u << EEINTCBit::VBLANKe); }

    // --- Interrupt query ------------------------------------------------------
    // True if any unmasked interrupt is pending (INTC_STAT & INTC_MASK != 0).
    bool AnyPending() const { return (intc_stat & intc_mask) != 0; }

    // --- Timer tick ----------------------------------------------------------
    // Advance all enabled timers by `bus_cycles` bus-clock ticks.
    // Raises the appropriate INTC_STAT bits for compare/overflow events.
    void TickTimers(std::uint32_t bus_cycles);

    // --- Hardware register read/write ----------------------------------------
    std::uint32_t Read32 (std::uint32_t pa) const;
    void          Write32(std::uint32_t pa, std::uint32_t val);
};

} // namespace armsx2::wasm
