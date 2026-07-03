#include "wasm_memory.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <sstream>

alignas(__pagealignsize) u8 eeHw[Ps2MemSize::Hardware] = {};
alignas(__pagealignsize) u8 iopHw[Ps2MemSize::IopHardware] = {};
EEVM_MemoryAllocMess* eeMem = nullptr;
IopVM_MemoryAllocMess* iopMem = nullptr;

namespace armsx2::wasm
{
namespace
{
std::unique_ptr<EEVM_MemoryAllocMess> s_ee_memory;
std::unique_ptr<IopVM_MemoryAllocMess> s_iop_memory;

std::string FormatHex(u32 value)
{
	std::ostringstream stream;
	stream << "0x" << std::hex << std::uppercase << value;
	return stream.str();
}

bool EnsureBootstrapMemory(std::string* error)
{
	if (!s_ee_memory)
		s_ee_memory.reset(new (std::nothrow) EEVM_MemoryAllocMess);
	if (!s_iop_memory)
		s_iop_memory.reset(new (std::nothrow) IopVM_MemoryAllocMess);

	if (!s_ee_memory || !s_iop_memory)
	{
		if (error)
			*error = "Failed to allocate the bootstrap EE/IOP memory backing.";
		return false;
	}

	eeMem = s_ee_memory.get();
	iopMem = s_iop_memory.get();
	return true;
}

template <typename Callback>
bool VisitEEMemory(u32 address, u32 size, Callback callback, std::string* error)
{
	if (size == 0)
		return true;

	u64 current_address = address;
	u32 remaining = size;
	while (remaining > 0)
	{
		u32 chunk = 0;
		if (current_address >= 0x70000000ull && current_address < 0x70004000ull)
		{
			const size_t offset = static_cast<size_t>(current_address - 0x70000000ull);
			chunk = std::min<u32>(remaining, static_cast<u32>(Ps2MemSize::Scratch - offset));
			callback(eeMem->Scratch + offset, chunk);
		}
		else
		{
			const u32 physical_address = static_cast<u32>(current_address) & 0x1FFFFFFF;
			if (physical_address < Ps2MemSize::MainRam)
			{
				chunk = std::min<u32>(remaining, Ps2MemSize::MainRam - physical_address);
				callback(eeMem->Main + physical_address, chunk);
			}
			else if (physical_address < Ps2MemSize::TotalRam)
			{
				const u32 offset = physical_address - Ps2MemSize::MainRam;
				chunk = std::min<u32>(remaining, Ps2MemSize::TotalRam - physical_address);
				callback(eeMem->ExtraMemory + offset, chunk);
			}
			else
			{
				if (error)
					*error = "Address " + FormatHex(static_cast<u32>(current_address)) + " is outside the bootstrap EE memory layout.";
				return false;
			}
		}

		current_address += chunk;
		remaining -= chunk;
	}

	return true;
}

template <typename Callback>
bool VisitIOPMemory(u32 address, u32 size, Callback callback, std::string* error)
{
	if (size == 0)
		return true;

	if (!iopMem)
	{
		if (error)
			*error = "IOP memory is unavailable.";
		return false;
	}

	u64 current_address = address;
	u32 remaining = size;
	while (remaining > 0)
	{
		const u32 offset = static_cast<u32>(current_address) & (Ps2MemSize::IopRam - 1);
		const u32 chunk = std::min<u32>(remaining, Ps2MemSize::IopRam - offset);
		callback(iopMem->Main + offset, chunk);
		current_address += chunk;
		remaining -= chunk;
	}

	return true;
}
} // namespace

bool ResetBootstrapPs2Memory(std::string* error)
{
	if (!EnsureBootstrapMemory(error))
		return false;

	std::memset(eeMem, 0, sizeof(*eeMem));
	std::memset(iopMem, 0, sizeof(*iopMem));
	std::memset(eeHw, 0, sizeof(eeHw));
	std::memset(iopHw, 0, sizeof(iopHw));
	return true;
}

bool WriteBootstrapEEMemory(u32 address, std::span<const u8> data, std::string* error)
{
	if (data.empty())
		return true;
	if (!EnsureBootstrapMemory(error))
		return false;

	size_t source_offset = 0;
	return VisitEEMemory(address, static_cast<u32>(data.size()),
		[&](u8* destination, u32 size)
		{
			std::memcpy(destination, data.data() + source_offset, size);
			source_offset += size;
		},
		error);
}

bool WriteBootstrapIOPMemory(u32 address, std::span<const u8> data, std::string* error)
{
	if (data.empty())
		return true;
	if (!EnsureBootstrapMemory(error))
		return false;

	size_t source_offset = 0;
	return VisitIOPMemory(address, static_cast<u32>(data.size()),
		[&](u8* destination, u32 size)
		{
			std::memcpy(destination, data.data() + source_offset, size);
			source_offset += size;
		},
		error);
}

bool ZeroBootstrapIOPMemory(u32 address, u32 size, std::string* error)
{
	if (size == 0)
		return true;
	if (!EnsureBootstrapMemory(error))
		return false;

	return VisitIOPMemory(address, size,
		[](u8* destination, u32 chunk_size)
		{
			std::memset(destination, 0, chunk_size);
		},
		error);
}
} // namespace armsx2::wasm
