#include "ir_interpreter.h"
#include "program_loader.h"
#include "wasm_memory.h"
#include "mips_to_ir.h"
#include "ee_ir_interpreter.h"
#include "ee_hw_regs.h"
#include "ee_state.h"
#include "iop_state.h"
#include "iop_hw_regs.h"
#include "iop_ir.h"
#include "iop_to_ir.h"
#include "iop_ir_interpreter.h"

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
#include <unordered_map>
#include <utility>

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
	std::string mounted_bios_path;

	// -- EE IR recompilation pipeline --
	armsx2::wasm::MipsLifter lifter;
	armsx2::wasm::EEIRInterpreter ee_interpreter;
	armsx2::wasm::EEState ee_state;
	armsx2::wasm::EEHWRegs ee_hw;          // INTC + timers
	armsx2::wasm::EEIRBlock ee_lifted_block;
	std::string ee_ir_summary;
	bool ee_ir_lifted = false;
	bool ee_running = false;
	std::uint64_t ee_blocks_executed = 0;
	std::uint64_t ee_ir_nodes_executed = 0;
	std::unordered_map<std::uint32_t, armsx2::wasm::EEIRBlock> ee_block_cache;

	armsx2::wasm::IopLifter iop_lifter;
	armsx2::wasm::IopIRInterpreter iop_interpreter;
	armsx2::wasm::IOPState iop_state;
	armsx2::wasm::IOPHWRegs iop_hw;           // interrupt controller (I_STAT/I_MASK)
	armsx2::wasm::IOPIRBlock iop_lifted_block;
	std::string iop_ir_summary;
	bool iop_ir_lifted = false;
	bool iop_running = false;
	std::uint64_t iop_blocks_executed = 0;
	std::uint64_t iop_ir_nodes_executed = 0;
	std::unordered_map<std::uint32_t, armsx2::wasm::IOPIRBlock> iop_block_cache;
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
		details = "Detected format: browser-backed BIOS candidate\nProbe: mounted and copied into the EE BIOS ROM-mapped region (0x1FC00000)\n";
	}

	g_app.browser_file_summary = BuildBrowserMountSummary(mounted_path, kind, valid, result, details, size_bytes);
	if (kind == BrowserFileKind::Bios && valid)
	{
		std::string bios_error;
		if (!armsx2::wasm::LoadBootstrapBiosImage(mounted_path, &bios_error))
		{
			g_app.browser_file_summary = BuildBrowserMountSummary(
				mounted_path, kind, false, "failed", std::string("Error: ") + bios_error + '\n', size_bytes);
			return 0;
		}

		g_app.mounted_bios_path = mounted_path;
	}

	return valid ? 1 : 0;
}

void ResetExecutionState()
{
	g_app.ee_block_cache.clear();
	g_app.iop_block_cache.clear();
	g_app.ee_blocks_executed = 0;
	g_app.iop_blocks_executed = 0;
	g_app.ee_ir_nodes_executed = 0;
	g_app.iop_ir_nodes_executed = 0;
	g_app.ee_ir_lifted = false;
	g_app.iop_ir_lifted = false;
	g_app.ee_ir_summary.clear();
	g_app.iop_ir_summary.clear();
}

bool PrepareExecution(const char* summary_reason)
{
	if (!armsx2::wasm::ResetBootstrapPs2Memory(&g_app.loaded_program.error))
	{
		g_app.ee_running = false;
		g_app.iop_running = false;
		g_app.ee_ir_summary = std::string("EE execution not started: ") + summary_reason + '\n';
		g_app.iop_ir_summary = std::string("IOP execution not started: ") + summary_reason + '\n';
		return false;
	}

	g_app.ee_state.Reset();
	g_app.iop_state.Reset();
	g_app.ee_hw = {};
	g_app.iop_hw = {};
	ResetExecutionState();
	return true;
}

void UpdateExecutionSummaries(const char* mode)
{
	std::ostringstream ee_out;
	ee_out << "--- EE IR Runtime ---\n";
	ee_out << "Mode: " << mode << "\n";
	ee_out << "Current PC: 0x" << std::hex << g_app.ee_state.pc << std::dec << "\n";
	ee_out << "Blocks executed: " << g_app.ee_blocks_executed << "\n";
	ee_out << "IR nodes executed: " << g_app.ee_ir_nodes_executed << "\n";
	ee_out << "Block cache entries: " << g_app.ee_block_cache.size() << "\n";
	ee_out << "Running: " << (g_app.ee_running ? "yes" : "no") << "\n";
	g_app.ee_ir_summary = ee_out.str();

	std::ostringstream iop_out;
	iop_out << "--- IOP IR Runtime ---\n";
	iop_out << "Mode: " << mode << "\n";
	iop_out << "Current PC: 0x" << std::hex << g_app.iop_state.pc << std::dec << "\n";
	iop_out << "Blocks executed: " << g_app.iop_blocks_executed << "\n";
	iop_out << "IR nodes executed: " << g_app.iop_ir_nodes_executed << "\n";
	iop_out << "Block cache entries: " << g_app.iop_block_cache.size() << "\n";
	iop_out << "Running: " << (g_app.iop_running ? "yes" : "no") << "\n";
	g_app.iop_ir_summary = iop_out.str();
}

void StepEEExecution(std::uint32_t block_budget)
{
	extern EEVM_MemoryAllocMess* eeMem;
	if (!g_app.ee_running || !eeMem)
		return;

	for (std::uint32_t step = 0; step < block_budget && g_app.ee_running; ++step)
	{
		auto cache_it = g_app.ee_block_cache.find(g_app.ee_state.pc);
		if (cache_it == g_app.ee_block_cache.end())
		{
			armsx2::wasm::EEIRBlock lifted = g_app.lifter.LiftBlock(
				reinterpret_cast<const std::uint8_t*>(eeMem),
				sizeof(EEVM_MemoryAllocMess),
				g_app.ee_state.pc);
			cache_it = g_app.ee_block_cache.emplace(g_app.ee_state.pc, std::move(lifted)).first;
		}

		g_app.ee_ir_lifted = true;
		auto run_result = g_app.ee_interpreter.Execute(
			cache_it->second, g_app.ee_state,
			reinterpret_cast<std::uint8_t*>(eeMem),
			sizeof(EEVM_MemoryAllocMess),
			&g_app.ee_hw);
		g_app.ee_state.pc = run_result.next_pc;
		g_app.ee_blocks_executed++;
		g_app.ee_ir_nodes_executed += run_result.instructions_run;

		if (run_result.interrupt_pending)
			g_app.ee_state.pc = 0x80000200u;

		if (run_result.halted || !run_result.error.empty())
		{
			g_app.ee_running = false;
			if (!run_result.error.empty())
				g_app.ee_ir_summary += std::string("Error: ") + run_result.error + "\n";
		}
	}
}

void StepIOPExecution(std::uint32_t block_budget)
{
	extern IopVM_MemoryAllocMess* iopMem;
	if (!g_app.iop_running || !iopMem)
		return;

	for (std::uint32_t step = 0; step < block_budget && g_app.iop_running; ++step)
	{
		auto cache_it = g_app.iop_block_cache.find(g_app.iop_state.pc);
		if (cache_it == g_app.iop_block_cache.end())
		{
			armsx2::wasm::IOPIRBlock lifted = g_app.iop_lifter.LiftBlock(
				reinterpret_cast<const std::uint8_t*>(iopMem),
				sizeof(IopVM_MemoryAllocMess),
				g_app.iop_state.pc);
			cache_it = g_app.iop_block_cache.emplace(g_app.iop_state.pc, std::move(lifted)).first;
		}

		g_app.iop_ir_lifted = true;
		auto run_result = g_app.iop_interpreter.Execute(
			cache_it->second, g_app.iop_state,
			reinterpret_cast<std::uint8_t*>(iopMem),
			sizeof(IopVM_MemoryAllocMess),
			&g_app.iop_hw);
		g_app.iop_state.pc = run_result.next_pc;
		g_app.iop_blocks_executed++;
		g_app.iop_ir_nodes_executed += run_result.instructions_run;

		if (run_result.interrupt_pending)
			g_app.iop_state.pc = 0x80000080u;

		if (run_result.halted || !run_result.error.empty())
		{
			g_app.iop_running = false;
			if (!run_result.error.empty())
				g_app.iop_ir_summary += std::string("Error: ") + run_result.error + "\n";
		}
	}
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

	StepEEExecution(64);
	StepIOPExecution(64);
	if (g_app.ee_running || g_app.iop_running)
		UpdateExecutionSummaries("continuous");

	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels(g_app.window, &width, &height);
	glViewport(0, 0, width, height);
	glClearColor(g_app.state.color[0], g_app.state.color[1], g_app.state.color[2], g_app.state.color[3]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	SDL_GL_SwapWindow(g_app.window);

	// Raise EE VBlank interrupts each frame (~60 Hz) so games and BIOS can
	// proceed past vsync wait loops.
	g_app.ee_hw.RaiseVBlankStart();
	g_app.ee_hw.RaiseVBlankEnd();
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

	if (g_app.loaded_program.valid && g_app.loaded_program.format == armsx2::wasm::ProgramFormat::PS2Elf)
	{
		g_app.ee_state.Reset();
		g_app.ee_state.pc = g_app.loaded_program.entry_point;
		g_app.iop_state.Reset();
		ResetExecutionState();
		g_app.ee_running = true;
		g_app.iop_running = true;
		UpdateExecutionSummaries("elf");
	}
	else
	{
		g_app.ee_running = false;
		g_app.iop_running = false;
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
const char* armsx2_wasm_get_iop_ir_summary()
{
	return g_app.iop_ir_summary.c_str();
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
int armsx2_wasm_boot_bios()
{
	if (g_app.mounted_bios_path.empty())
	{
		g_app.ee_ir_summary = "EE execution not started: mount a BIOS image first.\n";
		g_app.iop_ir_summary = "IOP execution not started: mount a BIOS image first.\n";
		return 0;
	}

	std::string bios_error;
	if (!armsx2::wasm::LoadBootstrapBiosImage(g_app.mounted_bios_path.c_str(), &bios_error))
	{
		g_app.ee_ir_summary = std::string("EE execution not started: ") + bios_error + '\n';
		g_app.iop_ir_summary = std::string("IOP execution not started: ") + bios_error + '\n';
		return 0;
	}

	if (!PrepareExecution("bootstrap memory allocation failed"))
		return 0;

	g_app.ee_state.pc = 0x1FC00000u;
	g_app.iop_state.pc = 0xBFC00000u;
	g_app.ee_running = true;
	g_app.iop_running = true;
	UpdateExecutionSummaries("bios");
	return 1;
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
