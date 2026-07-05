#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace armsx2::wasm
{
// ============================================================================
// IOP (R3000A / PS1 CPU) state — 32-bit MIPS I/R3000 derivative.
// Manages all PS2 I/O subsystems: controllers, memory cards, CDVD, SPU2, SIF.
// ============================================================================
struct IOPState
{
    std::array<std::uint32_t, 32> gpr = {};
    std::uint32_t hi = 0;
    std::uint32_t lo = 0;
    std::uint32_t pc = 0;
    std::array<std::uint32_t, 32> cop0 = {};
    std::uint32_t cycle = 0;
    bool halted = false;

    void Reset()
    {
        std::memset(this, 0, sizeof(*this));
        pc = 0xBFC00000u;
    }

    std::uint32_t ReadGPR(unsigned i) const { return (i == 0) ? 0u : gpr[i & 31]; }
    void WriteGPR(unsigned i, std::uint32_t v) { if (i != 0) gpr[i & 31] = v; }
};
} // namespace armsx2::wasm
