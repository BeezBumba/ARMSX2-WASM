#pragma once

#include <cstdint>

// ============================================================================
// IOP hardware register state — interrupt controller (I_STAT / I_MASK).
//
// Physical address map (after addr & 0x1FFFFFFF):
//   0x1F801070 : I_STAT — interrupt status (write-AND: writing 0 acknowledges)
//   0x1F801074 : I_MASK — interrupt mask   (plain read/write)
//
// An interrupt is signalled to the IOP CPU when (I_STAT & I_MASK) != 0,
// which sets the IP bit in COP0 Cause and may enter the exception handler
// if the global IE flag in COP0 Status is set.
// ============================================================================

namespace armsx2::wasm
{

// IOP interrupt channel bit indices (I_STAT / I_MASK)
namespace IOPIntBit
{
    constexpr std::uint32_t VBLANK  =  0;  // VBlank
    constexpr std::uint32_t GPU     =  1;  // GPU
    constexpr std::uint32_t CDVD    =  2;  // CDVD
    constexpr std::uint32_t DMA     =  3;  // DMA controller
    constexpr std::uint32_t RTC0    =  4;  // Root counter 0
    constexpr std::uint32_t RTC1    =  5;  // Root counter 1
    constexpr std::uint32_t RTC2    =  6;  // Root counter 2
    constexpr std::uint32_t SIO0    =  7;  // Serial I/O 0
    constexpr std::uint32_t SIO1    =  8;  // Serial I/O 1
    constexpr std::uint32_t SPU2    =  9;  // SPU2
    constexpr std::uint32_t PIO     = 10;  // PIO
    constexpr std::uint32_t EVBLANK = 11;  // End of VBlank
    constexpr std::uint32_t DVD     = 12;  // DVD
    constexpr std::uint32_t PCMCIA  = 13;  // PCMCIA
    constexpr std::uint32_t RTC3    = 14;  // Root counter 3
    constexpr std::uint32_t RTC4    = 15;  // Root counter 4
    constexpr std::uint32_t RTC5    = 16;  // Root counter 5
    constexpr std::uint32_t SIO2    = 17;  // Serial I/O 2
    constexpr std::uint32_t HTR0    = 18;  // USB host timer 0
    constexpr std::uint32_t HTR1    = 19;  // USB host timer 1
    constexpr std::uint32_t HTR2    = 20;  // USB host timer 2
    constexpr std::uint32_t HTR3    = 21;  // USB host timer 3
    constexpr std::uint32_t USB     = 22;  // USB
    constexpr std::uint32_t EXCP    = 23;  // Exception / software
} // namespace IOPIntBit

struct IOPHWRegs
{
    std::uint32_t i_stat = 0;   // I_STAT: interrupt status (0x1F801070)
    std::uint32_t i_mask = 0;   // I_MASK: interrupt mask   (0x1F801074)

    // True if any unmasked interrupt is pending (I_STAT & I_MASK != 0).
    bool AnyPending() const { return (i_stat & i_mask) != 0; }

    // Raise an interrupt channel (sets the corresponding I_STAT bit).
    void RaiseInterrupt(std::uint32_t bit) { i_stat |= (1u << bit); }

    // Hardware register read/write (physical address, i.e. vaddr & 0x1FFFFFFF).
    std::uint32_t Read32 (std::uint32_t pa) const;
    void          Write32(std::uint32_t pa, std::uint32_t val);
};

} // namespace armsx2::wasm
