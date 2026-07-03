#pragma once

#include "ee_state.h"
#include "ir_bytecode.h"

#include <cstdint>
#include <string>

namespace armsx2::wasm
{
// ============================================================================
// EE IR Interpreter — executes EEIRBlock nodes against an EEState and flat
// memory buffers.  This is the validation backend: it gives correct results
// but makes no attempt at speed.  A future WASM emitter will replace it for
// performance.
// ============================================================================
class EEIRInterpreter
{
public:
	struct RunResult
	{
		std::uint32_t next_pc = 0;         // PC to continue from after this block
		std::uint32_t instructions_run = 0;
		bool halted = false;               // hit BREAK / unhandled trap
		std::string error;                 // non-empty on fatal error
	};

	// Execute one IR block.  On return, `state.pc` has NOT been updated — the
	// caller should set `state.pc = result.next_pc` to advance execution.
	RunResult Execute(const EEIRBlock& block, EEState& state,
					  std::uint8_t* ee_memory, std::uint32_t ee_memory_size) const;

private:
	// Memory helpers (flat physical model: addr & 0x1FFF'FFFF).
	static std::uint8_t  ReadMem8 (const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr);
	static std::uint16_t ReadMem16(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr);
	static std::uint32_t ReadMem32(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr);
	static std::uint64_t ReadMem64(const std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr);

	static void WriteMem8 (std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint8_t  val);
	static void WriteMem16(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint16_t val);
	static void WriteMem32(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint32_t val);
	static void WriteMem64(std::uint8_t* mem, std::uint32_t sz, std::uint32_t vaddr, std::uint64_t val);
};
} // namespace armsx2::wasm
