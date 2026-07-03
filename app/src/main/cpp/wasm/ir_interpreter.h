#pragma once

#include "ir_bytecode.h"

#include <array>

namespace armsx2::wasm
{
struct IRExecutionState
{
	std::array<float, 8> registers = {};
	std::array<float, 4> color = {0.08f, 0.10f, 0.16f, 1.0f};
};

class IRInterpreter
{
public:
	void Execute(const IRProgram& program, IRExecutionState& state) const;
};

IRProgram CreateBootstrapProgram();
} // namespace armsx2::wasm
