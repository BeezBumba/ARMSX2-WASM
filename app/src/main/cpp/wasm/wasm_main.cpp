#include "ir_interpreter.h"
#include "program_loader.h"
#include "wasm_memory.h"
#include "mips_to_ir.h"
#include "ee_ir_interpreter.h"
#include "ee_state.h"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <GLES3/gl3.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace
{
// Cap browser-backed disc imports at 4.7 GB, matching the common single-layer DVD game image target for this bootstrap work.
constexpr std::uint64_t s_max_browser_disc_import_bytes = 4700000000ull;
constexpr size_t s_iso9660_sector_size = 2048;
constexpr off_t s_iso9660_primary_volume_descriptor_offset = static_cast<off_t>(16 * s_iso9660_sector_size);
constexpr std::uint8_t s_iso9660_primary_volume_descriptor_type = 1;
constexpr std::array<char, 5> s_iso9660_standard_identifier = {'C', 'D', '0', '0', '1'};
constexpr size_t s_iso9660_volume_id_offset = 40;
constexpr size_t s_iso9660_volume_id_length = 32;

struct WasmBootstrapApp
{
	SDL_Window* window = nullptr;
	SDL_GLContext context = nullptr;
	armsx2::wasm::IRInterpreter interpreter;
	armsx2::wasm::IRProgram program = armsx2::wasm::CreateBootstrapProgram();
	armsx2::wasm::IRExecutionState state;
	armsx2::wasm::ProgramLoader loader;
	armsx2::wasm::LoadedProgramImage loaded_program;
	std::string browser_file_summary;

	// -- EE IR recompilation pipeline --
	armsx2::wasm::MipsLifter lifter;
	armsx2::wasm::EEIRInterpreter ee_interpreter;
	armsx2::wasm::EEState ee_state;
	armsx2::wasm::EEIRBlock ee_lifted_block;
	std::string ee_ir_summary;
	bool ee_ir_lifted = false;
};

WasmBootstrapApp g_app;
bool g_running = true;

enum class BrowserFileKind
{
	Bios,
	Game,
};

std::string_view GetFileName(std::string_view path)
{
	const size_t separator = path.find_last_of('/');
	return (separator == std::string_view::npos) ? path : path.substr(separator + 1);
}

std::string FormatByteSize(std::uint64_t size)
{
	static constexpr const char* units[] = {"bytes", "KiB", "MiB", "GiB", "TiB"};
	double value = static_cast<double>(size);
	size_t unit_index = 0;
	while (value >= 1024.0 && unit_index < (std::size(units) - 1))
	{
		value /= 1024.0;
		unit_index++;
	}

	std::ostringstream stream;
	stream.setf(std::ios::fixed, std::ios::floatfield);
	stream.precision(unit_index == 0 ? 0 : 2);
	stream << value << ' ' << units[unit_index] << " (" << size << " bytes)";
	return stream.str();
}

std::string TrimAscii(std::string_view value)
{
	size_t start = 0;
	while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
		start++;

	size_t end = value.size();
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
		end--;

	return std::string(value.substr(start, end - start));
}

std::string BuildBrowserMountSummary(const char* mounted_path, BrowserFileKind kind, bool valid,
	const char* result, const std::string& details, std::uint64_t size_bytes)
{
	std::ostringstream stream;
	stream << "ARMSX2 WASM browser-backed file mount\n";
	stream << "Kind: " << (kind == BrowserFileKind::Bios ? "BIOS image" : "Game image") << '\n';
	stream << "Result: " << result << '\n';
	stream << "Mounted path: " << mounted_path << '\n';
	stream << "File name: " << GetFileName(mounted_path) << '\n';
	stream << "Browser-backed mount: WORKERFS (read-only, on-demand reads, no JS heap copy)\n";
	stream << "Size: " << FormatByteSize(size_bytes) << '\n';
	if (kind == BrowserFileKind::Game)
		stream << "Large file target: supports mounted disc images up to 4.7 GB.\n";
	stream << details;
	if (!details.empty() && details.back() != '\n')
		stream << '\n';
	stream << "Ready: " << (valid ? "yes" : "no") << '\n';
	return stream.str();
}

bool TryInspectIso9660(const char* mounted_path, std::uint64_t size_bytes, std::string* details)
{
	if (size_bytes < (17ull * s_iso9660_sector_size))
		return false;

	FILE* file = std::fopen(mounted_path, "rb");
	if (!file)
		return false;

	std::array<std::uint8_t, s_iso9660_sector_size> sector = {};
	const bool seek_ok = (fseeko(file, s_iso9660_primary_volume_descriptor_offset, SEEK_SET) == 0);
	const size_t bytes_read = seek_ok ? std::fread(sector.data(), 1, sector.size(), file) : 0;
	std::fclose(file);

	if (!seek_ok || bytes_read != sector.size())
		return false;

	if (sector[0] != s_iso9660_primary_volume_descriptor_type ||
		std::memcmp(sector.data() + 1, s_iso9660_standard_identifier.data(), s_iso9660_standard_identifier.size()) != 0)
		return false;

	const std::string volume_id = TrimAscii(std::string_view(
		reinterpret_cast<const char*>(sector.data() + s_iso9660_volume_id_offset), s_iso9660_volume_id_length));
	std::ostringstream stream;
	stream << "Detected format: ISO9660 primary volume descriptor\n";
	if (!volume_id.empty())
		stream << "Volume ID: " << volume_id << '\n';
	else
		stream << "Volume ID: <blank>\n";
	stream << "Probe: sector 16 header read through the browser-backed mount\n";
	*details = stream.str();
	return true;
}

bool StatMountedFile(const char* mounted_path, std::uint64_t* size_bytes, std::string* error)
{
	struct stat file_stats = {};
	if (stat(mounted_path, &file_stats) != 0)
	{
		if (error)
			*error = "Failed to stat the mounted browser file.";
		return false;
	}

	if (file_stats.st_size < 0)
	{
		if (error)
			*error = "Mounted file size is invalid.";
		return false;
	}

	*size_bytes = static_cast<std::uint64_t>(file_stats.st_size);
	return true;
}

int MountBrowserFile(const char* mounted_path, BrowserFileKind kind)
{
	if (!mounted_path || mounted_path[0] == '\0')
	{
		g_app.browser_file_summary = BuildBrowserMountSummary("<none>", kind, false, "failed",
			"Error: No mounted browser file path was provided.\n", 0);
		return 0;
	}

	std::uint64_t size_bytes = 0;
	std::string error;
	if (!StatMountedFile(mounted_path, &size_bytes, &error))
	{
		g_app.browser_file_summary = BuildBrowserMountSummary(
			mounted_path, kind, false, "failed", std::string("Error: ") + error + '\n', 0);
		return 0;
	}

	std::string details;
	bool valid = true;
	const char* result = "mounted";
	if (kind == BrowserFileKind::Game)
	{
		if (size_bytes > s_max_browser_disc_import_bytes)
		{
			valid = false;
			result = "failed";
			details = "Error: Game image exceeds the current 4.7 GB browser import limit.\n";
		}
		else if (!TryInspectIso9660(mounted_path, size_bytes, &details))
		{
			details = "Detected format: browser-backed disc image\nProbe: mounted for future disc reads without copying the full image into memory\n";
		}
	}
	else
	{
		details = "Detected format: browser-backed BIOS candidate\nProbe: mounted for future BIOS file access without copying the ROM into the WASM heap\n";
	}

	g_app.browser_file_summary = BuildBrowserMountSummary(mounted_path, kind, valid, result, details, size_bytes);
	return valid ? 1 : 0;
}

bool InitializeApp()
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
	{
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	g_app.window = SDL_CreateWindow("ARMSX2 WASM Bootstrap", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!g_app.window)
	{
		std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return false;
	}

	g_app.context = SDL_GL_CreateContext(g_app.window);
	if (!g_app.context)
	{
		std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_GL_MakeCurrent(g_app.window, g_app.context);
	SDL_GL_SetSwapInterval(1);
	return true;
}

void ShutdownApp()
{
	if (g_app.context)
	{
		SDL_GL_DestroyContext(g_app.context);
		g_app.context = nullptr;
	}

	if (g_app.window)
	{
		SDL_DestroyWindow(g_app.window);
		g_app.window = nullptr;
	}

	SDL_Quit();
}

void TickApp()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_EVENT_QUIT)
		{
			g_running = false;
#if defined(__EMSCRIPTEN__)
			emscripten_cancel_main_loop();
#endif
			ShutdownApp();
			return;
		}
	}

	if (g_app.loaded_program.valid)
	{
		g_app.state.color = g_app.loaded_program.accent_color;
	}
	else
	{
		g_app.interpreter.Execute(g_app.program, g_app.state);
	}

	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels(g_app.window, &width, &height);
	glViewport(0, 0, width, height);
	glClearColor(g_app.state.color[0], g_app.state.color[1], g_app.state.color[2], g_app.state.color[3]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	SDL_GL_SwapWindow(g_app.window);
}
} // namespace

extern "C"
{
#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
int armsx2_wasm_load_program(const std::uint8_t* data, size_t size)
{
	if (!data || size == 0)
	{
		g_app.loaded_program = {};
		g_app.loaded_program.error = "No executable payload was provided to the WASM loader.";
		g_app.loaded_program.summary = "ARMSX2 WASM executable loader\nLoad result: failed\nError: No executable payload was provided to the WASM loader.\n";
		g_app.loaded_program.accent_color = {0.34f, 0.10f, 0.10f, 1.0f};
		return 0;
	}

	g_app.loaded_program = g_app.loader.LoadFromBytes(std::span<const u8>(reinterpret_cast<const u8*>(data), size));

	// If the ELF loaded successfully, lift the entry block to IR.
	if (g_app.loaded_program.valid && g_app.loaded_program.format == armsx2::wasm::ProgramFormat::PS2Elf)
	{
		// Use the flat EE memory backing for lifting.
		extern EEVM_MemoryAllocMess* eeMem;
		if (eeMem)
		{
			g_app.ee_state.Reset();
			g_app.ee_state.pc = g_app.loaded_program.entry_point;

			// Lift the entry basic block.
			g_app.ee_lifted_block = g_app.lifter.LiftBlock(
				reinterpret_cast<const std::uint8_t*>(eeMem),
				sizeof(EEVM_MemoryAllocMess),
				g_app.loaded_program.entry_point);

			g_app.ee_ir_lifted = true;

			// Run the lifted block through the IR interpreter.
			auto run_result = g_app.ee_interpreter.Execute(
				g_app.ee_lifted_block, g_app.ee_state,
				reinterpret_cast<std::uint8_t*>(eeMem),
				sizeof(EEVM_MemoryAllocMess));

			// Build a summary of the IR lift + run.
			std::ostringstream ir_out;
			ir_out << "--- EE IR Pipeline ---\n";
			ir_out << "Entry PC: 0x" << std::hex << g_app.loaded_program.entry_point << "\n";
			ir_out << "Block range: 0x" << g_app.ee_lifted_block.start_pc
				   << " .. 0x" << g_app.ee_lifted_block.end_pc << "\n" << std::dec;
			ir_out << "IR nodes emitted: " << g_app.ee_lifted_block.instructions.size() << "\n";
			ir_out << "IR nodes executed: " << run_result.instructions_run << "\n";
			ir_out << "Next PC: 0x" << std::hex << run_result.next_pc << "\n" << std::dec;
			if (run_result.halted)
				ir_out << "Halted: yes\n";
			if (!run_result.error.empty())
				ir_out << "Error: " << run_result.error << "\n";
			ir_out << "\n" << g_app.ee_lifted_block.Dump();
			g_app.ee_ir_summary = ir_out.str();

			std::fprintf(stdout, "%s\n", g_app.ee_ir_summary.c_str());
		}
	}

	return g_app.loaded_program.valid ? 1 : 0;
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
const char* armsx2_wasm_get_program_summary()
{
	return g_app.loaded_program.summary.c_str();
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
const char* armsx2_wasm_get_ir_summary()
{
	return g_app.ee_ir_summary.c_str();
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
int armsx2_wasm_mount_bios(const char* mounted_path)
{
	return MountBrowserFile(mounted_path, BrowserFileKind::Bios);
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
int armsx2_wasm_mount_game(const char* mounted_path)
{
	return MountBrowserFile(mounted_path, BrowserFileKind::Game);
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
const char* armsx2_wasm_get_browser_file_summary()
{
	return g_app.browser_file_summary.c_str();
}
}

int main()
{
	if (!InitializeApp())
		return 1;

#if defined(__EMSCRIPTEN__)
	emscripten_set_main_loop(TickApp, 0, 1);
#else
	while (g_running)
		TickApp();
#endif

	return 0;
}
