#include "ir_interpreter.h"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <GLES3/gl3.h>

#include <array>
#include <cstdio>

namespace
{
struct WasmBootstrapApp
{
	SDL_Window* window = nullptr;
	SDL_GLContext context = nullptr;
	armsx2::wasm::IRInterpreter interpreter;
	armsx2::wasm::IRProgram program = armsx2::wasm::CreateBootstrapProgram();
	armsx2::wasm::IRExecutionState state;
};

WasmBootstrapApp g_app;
bool g_running = true;

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

	g_app.interpreter.Execute(g_app.program, g_app.state);

	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels(g_app.window, &width, &height);
	glViewport(0, 0, width, height);
	glClearColor(g_app.state.color[0], g_app.state.color[1], g_app.state.color[2], g_app.state.color[3]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	SDL_GL_SwapWindow(g_app.window);
}
} // namespace

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
