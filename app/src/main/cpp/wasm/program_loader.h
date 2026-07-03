#pragma once

#include "pcsx2/Elfheader.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace armsx2::wasm
{
enum class ProgramFormat
{
	Unknown,
	PS2Elf,
	PSXExe,
};

struct ProgramSegment
{
	u32 virtual_address = 0;
	u32 file_size = 0;
	u32 memory_size = 0;
	u32 flags = 0;
};

struct LoadedProgramImage
{
	bool valid = false;
	ProgramFormat format = ProgramFormat::Unknown;
	std::string error;
	std::string summary;
	u32 entry_point = 0;
	u32 crc = 0;
	u32 load_start = 0;
	u32 load_end = 0;
	u32 text_start = 0;
	u32 text_size = 0;
	u32 program_header_count = 0;
	u32 loadable_segment_count = 0;
	u32 ee_bytes_written = 0;
	u32 iop_bytes_written = 0;
	u32 zero_filled_bytes = 0;
	std::vector<ProgramSegment> segments;
	std::array<float, 4> accent_color = {0.08f, 0.10f, 0.16f, 1.0f};
};

class ProgramLoader
{
public:
	LoadedProgramImage LoadFromBytes(std::span<const u8> data) const;

private:
	static u32 CalculateCRC(std::span<const u8> data);
	static std::string BuildSummary(const LoadedProgramImage& image);

	LoadedProgramImage LoadElf(std::span<const u8> data) const;
	LoadedProgramImage LoadPSXExe(std::span<const u8> data) const;
};
} // namespace armsx2::wasm
