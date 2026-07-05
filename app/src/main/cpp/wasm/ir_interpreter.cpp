#include "ir_interpreter.h"

namespace armsx2::wasm
{
void IRInterpreter::Execute(const IRProgram& program, IRExecutionState& state) const
{
	state.color = program.color;

	for (const IRInstruction& instruction : program.instructions)
	{
		switch (instruction.opcode)
		{
			case IROpcode::Nop:
				break;

			case IROpcode::LoadImmF32:
				if (instruction.dst < state.registers.size())
					state.registers[instruction.dst] = instruction.immediate;
				break;

			case IROpcode::AddF32:
				if (instruction.dst < state.registers.size() &&
					instruction.src0 < state.registers.size() &&
					instruction.src1 < state.registers.size())
				{
					state.registers[instruction.dst] =
						state.registers[instruction.src0] + state.registers[instruction.src1];
				}
				break;

			case IROpcode::StoreColor:
				if (instruction.dst < state.color.size() && instruction.src0 < state.registers.size())
					state.color[instruction.dst] = state.registers[instruction.src0];
				break;

			case IROpcode::Halt:
				return;
		}
	}
}

IRProgram CreateBootstrapProgram()
{
	IRProgram program;
	program.instructions = {
		{IROpcode::LoadImmF32, 0, 0, 0, 0.10f},
		{IROpcode::LoadImmF32, 1, 0, 0, 0.18f},
		{IROpcode::LoadImmF32, 2, 0, 0, 0.30f},
		{IROpcode::LoadImmF32, 3, 0, 0, 1.00f},
		{IROpcode::StoreColor, 0, 0, 0, 0.0f},
		{IROpcode::StoreColor, 1, 1, 0, 0.0f},
		{IROpcode::StoreColor, 2, 2, 0, 0.0f},
		{IROpcode::StoreColor, 3, 3, 0, 0.0f},
		{IROpcode::Halt, 0, 0, 0, 0.0f},
	};
	return program;
}
} // namespace armsx2::wasm
