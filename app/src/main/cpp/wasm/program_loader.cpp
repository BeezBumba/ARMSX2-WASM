#include "program_loader.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace armsx2::wasm
{
namespace
{
constexpr std::array<u8, 4> s_elf_magic = {0x7F, 'E', 'L', 'F'};
constexpr std::array<char, 8> s_psx_magic = {'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
constexpr u32 s_loadable_segment_type = 1;
constexpr u32 s_max_image_span = 256u * 1024u * 1024u;

bool AddOverflows(u32 lhs, u32 rhs)
{
	return rhs > (std::numeric_limits<u32>::max() - lhs);
}

bool RangeFits(std::span<const u8> data, u32 offset, u32 size)
{
	if (AddOverflows(offset, size))
		return false;

	return (static_cast<size_t>(offset) + static_cast<size_t>(size)) <= data.size();
}

std::string FormatHex(u32 value)
{
	std::ostringstream stream;
	stream << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
	return stream.str();
}
} // namespace

LoadedProgramImage ProgramLoader::LoadFromBytes(std::span<const u8> data) const
{
	if (data.size() >= s_elf_magic.size() &&
		std::equal(s_elf_magic.begin(), s_elf_magic.end(), data.begin()))
	{
		return LoadElf(data);
	}

	if (data.size() >= s_psx_magic.size() &&
		std::equal(s_psx_magic.begin(), s_psx_magic.end(), reinterpret_cast<const char*>(data.data())))
	{
		return LoadPSXExe(data);
	}

	LoadedProgramImage image;
	image.error = "Unsupported executable format. Expected a PS2 ELF or PS-X EXE image.";
	image.summary = BuildSummary(image);
	image.accent_color = {0.34f, 0.10f, 0.10f, 1.0f};
	return image;
}

u32 ProgramLoader::CalculateCRC(std::span<const u8> data)
{
	u32 crc = 0;
	const size_t word_count = data.size() / sizeof(u32);
	for (size_t index = 0; index < word_count; index++)
	{
		u32 word = 0;
		std::memcpy(&word, data.data() + (index * sizeof(u32)), sizeof(word));
		crc ^= word;
	}

	return crc;
}

LoadedProgramImage ProgramLoader::LoadElf(std::span<const u8> data) const
{
	LoadedProgramImage image;
	image.format = ProgramFormat::PS2Elf;
	image.crc = CalculateCRC(data);
	image.accent_color = {0.10f, 0.22f, 0.36f, 1.0f};

	if (data.size() < sizeof(ELF_HEADER))
	{
		image.error = "ELF image is smaller than the expected header size.";
		image.summary = BuildSummary(image);
		return image;
	}

	ELF_HEADER header = {};
	std::memcpy(&header, data.data(), sizeof(header));
	image.entry_point = header.e_entry;
	image.program_header_count = header.e_phnum;

	if (header.e_ident[4] != 1 || header.e_ident[5] != 1)
	{
		image.error = "Only 32-bit little-endian ELF images are supported by this bootstrap.";
		image.summary = BuildSummary(image);
		return image;
	}

	if (header.e_phnum == 0)
	{
		image.error = "ELF image does not contain any program headers.";
		image.summary = BuildSummary(image);
		return image;
	}

	if (header.e_phentsize != sizeof(ELF_PHR))
	{
		image.error = "ELF program header entries do not match the PS2 loader layout.";
		image.summary = BuildSummary(image);
		return image;
	}

	if (!RangeFits(data, header.e_phoff, static_cast<u32>(header.e_phnum) * sizeof(ELF_PHR)))
	{
		image.error = "ELF program header table extends beyond the uploaded file.";
		image.summary = BuildSummary(image);
		return image;
	}

	u32 load_start = std::numeric_limits<u32>::max();
	u32 load_end = 0;
	for (u32 index = 0; index < header.e_phnum; index++)
	{
		ELF_PHR program_header = {};
		const size_t program_offset = static_cast<size_t>(header.e_phoff) + (static_cast<size_t>(index) * sizeof(ELF_PHR));
		std::memcpy(&program_header, data.data() + program_offset, sizeof(program_header));

		if (program_header.p_type != s_loadable_segment_type)
			continue;

		if (!RangeFits(data, program_header.p_offset, program_header.p_filesz))
		{
			image.error = "ELF segment data extends beyond the uploaded file.";
			image.summary = BuildSummary(image);
			return image;
		}

		if (program_header.p_memsz < program_header.p_filesz)
		{
			image.error = "ELF segment memory size is smaller than its file size.";
			image.summary = BuildSummary(image);
			return image;
		}

		if (AddOverflows(program_header.p_vaddr, program_header.p_memsz))
		{
			image.error = "ELF segment address range overflows the 32-bit PS2 address space.";
			image.summary = BuildSummary(image);
			return image;
		}

		load_start = std::min(load_start, program_header.p_vaddr);
		load_end = std::max(load_end, program_header.p_vaddr + program_header.p_memsz);

		image.segments.push_back({
			.virtual_address = program_header.p_vaddr,
			.file_size = program_header.p_filesz,
			.memory_size = program_header.p_memsz,
			.flags = program_header.p_flags,
		});

		if (program_header.p_vaddr <= header.e_entry &&
			(program_header.p_vaddr + program_header.p_memsz) > header.e_entry)
		{
			image.text_start = program_header.p_vaddr;
			image.text_size = program_header.p_memsz;
		}
	}

	image.loadable_segment_count = static_cast<u32>(image.segments.size());
	if (image.segments.empty())
	{
		image.error = "ELF image has no PT_LOAD program segments to map.";
		image.summary = BuildSummary(image);
		return image;
	}

	image.load_start = load_start;
	image.load_end = load_end;

	if ((load_end - load_start) > s_max_image_span)
	{
		image.error = "ELF load span is too large for the current WASM bootstrap memory image.";
		image.summary = BuildSummary(image);
		return image;
	}

	image.memory_image.resize(load_end - load_start);
	for (u32 index = 0; index < header.e_phnum; index++)
	{
		ELF_PHR program_header = {};
		const size_t program_offset = static_cast<size_t>(header.e_phoff) + (static_cast<size_t>(index) * sizeof(ELF_PHR));
		std::memcpy(&program_header, data.data() + program_offset, sizeof(program_header));

		if (program_header.p_type != s_loadable_segment_type || program_header.p_memsz == 0)
			continue;

		const size_t destination_offset = static_cast<size_t>(program_header.p_vaddr - load_start);
		std::memcpy(image.memory_image.data() + destination_offset,
			data.data() + program_header.p_offset, program_header.p_filesz);
	}

	image.valid = true;
	image.summary = BuildSummary(image);
	return image;
}

LoadedProgramImage ProgramLoader::LoadPSXExe(std::span<const u8> data) const
{
	LoadedProgramImage image;
	image.format = ProgramFormat::PSXExe;
	image.crc = CalculateCRC(data);
	image.accent_color = {0.28f, 0.18f, 0.08f, 1.0f};

	if (data.size() < sizeof(PSXEXEHeader))
	{
		image.error = "PS-X EXE image is smaller than the required 0x800-byte header.";
		image.summary = BuildSummary(image);
		return image;
	}

	PSXEXEHeader header = {};
	std::memcpy(&header, data.data(), sizeof(header));
	image.entry_point = header.initial_pc;

	u32 payload_size = header.file_size;
	const u32 available_payload = static_cast<u32>(data.size() - sizeof(PSXEXEHeader));
	if (payload_size > available_payload)
		payload_size = available_payload;

	if (AddOverflows(header.load_address, payload_size))
	{
		image.error = "PS-X EXE load range overflows the 32-bit address space.";
		image.summary = BuildSummary(image);
		return image;
	}

	const u32 load_end = header.load_address + payload_size;
	const u32 memfill_end = AddOverflows(header.memfill_start, header.memfill_size) ?
		header.memfill_start : (header.memfill_start + header.memfill_size);
	const u32 image_start = std::min(header.load_address, header.memfill_size > 0 ? header.memfill_start : header.load_address);
	const u32 image_end = std::max(load_end, memfill_end);

	if (image_end < image_start)
	{
		image.error = "PS-X EXE load range overflows the 32-bit address space.";
		image.summary = BuildSummary(image);
		return image;
	}

	if ((image_end - image_start) > s_max_image_span)
	{
		image.error = "PS-X EXE load span is too large for the current WASM bootstrap memory image.";
		image.summary = BuildSummary(image);
		return image;
	}

	image.load_start = image_start;
	image.load_end = image_end;
	image.text_start = header.load_address;
	image.text_size = payload_size;
	image.segments.push_back({
		.virtual_address = header.load_address,
		.file_size = payload_size,
		.memory_size = payload_size,
		.flags = 0,
	});
	if (header.memfill_size > 0)
	{
		image.segments.push_back({
			.virtual_address = header.memfill_start,
			.file_size = 0,
			.memory_size = header.memfill_size,
			.flags = 0,
		});
	}

	image.program_header_count = static_cast<u32>(image.segments.size());
	image.loadable_segment_count = static_cast<u32>(image.segments.size());
	image.memory_image.resize(image_end - image_start);
	std::memcpy(image.memory_image.data() + static_cast<size_t>(header.load_address - image_start),
		data.data() + sizeof(PSXEXEHeader), payload_size);

	image.valid = true;
	image.summary = BuildSummary(image);
	return image;
}

std::string ProgramLoader::BuildSummary(const LoadedProgramImage& image)
{
	std::ostringstream stream;
	stream << "ARMSX2 WASM executable loader\n";

	switch (image.format)
	{
		case ProgramFormat::PS2Elf:
			stream << "Format: PS2 ELF\n";
			break;

		case ProgramFormat::PSXExe:
			stream << "Format: PS-X EXE\n";
			break;

		default:
			stream << "Format: Unknown\n";
			break;
	}

	stream << "Load result: " << (image.valid ? "ready" : "failed") << '\n';
	if (!image.error.empty())
		stream << "Error: " << image.error << '\n';

	stream << "Entry point: " << FormatHex(image.entry_point) << '\n';
	stream << "CRC: " << FormatHex(image.crc) << '\n';
	stream << "Program headers: " << image.program_header_count << '\n';
	stream << "Loadable segments: " << image.loadable_segment_count << '\n';
	stream << "Load range: " << FormatHex(image.load_start) << " - " << FormatHex(image.load_end) << '\n';
	stream << "Mapped image bytes: " << image.memory_image.size() << '\n';

	if (image.text_size > 0)
		stream << "Entry segment: " << FormatHex(image.text_start) << " (" << image.text_size << " bytes)\n";

	for (size_t index = 0; index < image.segments.size(); index++)
	{
		const ProgramSegment& segment = image.segments[index];
		stream << "Segment " << index
			<< ": vaddr=" << FormatHex(segment.virtual_address)
			<< " file=" << segment.file_size
			<< " mem=" << segment.memory_size
			<< " flags=" << FormatHex(segment.flags)
			<< '\n';
	}

	return stream.str();
}
} // namespace armsx2::wasm
