#pragma once

#include "pcsx2/MemoryTypes.h"

#include <cstdint>
#include <span>
#include <string>

namespace armsx2::wasm
{
bool ResetBootstrapPs2Memory(std::string* error);

bool WriteBootstrapEEMemory(u32 address, std::span<const u8> data, std::string* error);
bool WriteBootstrapIOPMemory(u32 address, std::span<const u8> data, std::string* error);
bool ZeroBootstrapIOPMemory(u32 address, u32 size, std::string* error);

u32 TranslateBootstrapEEPhysicalAddress(u32 virtual_address);

bool LoadBootstrapBiosImage(const char* mounted_path, std::string* error);
bool ReadBootstrapEEMemory(u32 address, u8* destination, u32 size);
u32 GetBootstrapBiosSize();
} // namespace armsx2::wasm
