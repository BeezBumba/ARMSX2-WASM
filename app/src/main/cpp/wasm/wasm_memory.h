#pragma once

#include "pcsx2/MemoryTypes.h"

#include <span>
#include <string>

namespace armsx2::wasm
{
bool ResetBootstrapPs2Memory(std::string* error);

bool WriteBootstrapEEMemory(u32 address, std::span<const u8> data, std::string* error);
bool WriteBootstrapIOPMemory(u32 address, std::span<const u8> data, std::string* error);
bool ZeroBootstrapIOPMemory(u32 address, u32 size, std::string* error);
} // namespace armsx2::wasm
