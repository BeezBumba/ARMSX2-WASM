cmake_minimum_required(VERSION 3.16)

if(NOT EMSCRIPTEN)
	message(FATAL_ERROR "The wasm bootstrap target must be built with the Emscripten toolchain.")
endif()

add_executable(armsx2-wasm-bootstrap
	wasm_main.cpp
	ir_interpreter.cpp
	program_loader.cpp
	wasm_memory.cpp
	mips_to_ir.cpp
	ee_ir_interpreter.cpp
)

target_include_directories(armsx2-wasm-bootstrap PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}
)

target_compile_features(armsx2-wasm-bootstrap PRIVATE cxx_std_20)
target_compile_definitions(armsx2-wasm-bootstrap PRIVATE
	ARMSX2_WASM_BOOTSTRAP=1
	ARMSX2_WASM_INTERPRETER_ONLY=1
)

target_link_libraries(armsx2-wasm-bootstrap PRIVATE SDL3::SDL3)

target_link_options(armsx2-wasm-bootstrap PRIVATE
	"-lidbfs.js"
	"-lworkerfs.js"
	"SHELL:-sUSE_WEBGL2=1"
	"SHELL:-sMAX_WEBGL_VERSION=2"
	"SHELL:-sMIN_WEBGL_VERSION=2"
	"SHELL:-sFULL_ES3=1"
	"SHELL:-sALLOW_MEMORY_GROWTH=1"
	"SHELL:-sFORCE_FILESYSTEM=1"
	"SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_armsx2_wasm_load_program','_armsx2_wasm_get_program_summary','_armsx2_wasm_get_ir_summary','_armsx2_wasm_mount_bios','_armsx2_wasm_mount_game','_armsx2_wasm_get_browser_file_summary']"
	"SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','ccall']"
	"SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/wasm_host.js"
	"SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/index.html"
)

set_target_properties(armsx2-wasm-bootstrap PROPERTIES
	OUTPUT_NAME "armsx2-wasm"
	SUFFIX ".html"
)
