#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace armsx2::wasm
{
enum class IROpcode : std::uint8_t
{
	Nop,
	LoadImmF32,
	AddF32,
	StoreColor,
	Halt,
};

struct IRInstruction
{
	IROpcode opcode = IROpcode::Nop;
	std::uint8_t dst = 0;
	std::uint8_t src0 = 0;
	std::uint8_t src1 = 0;
	float immediate = 0.0f;
};

struct IRProgram
{
	std::vector<IRInstruction> instructions;
	std::array<float, 4> color = {0.08f, 0.10f, 0.16f, 1.0f};
};
} // namespace armsx2::wasm
