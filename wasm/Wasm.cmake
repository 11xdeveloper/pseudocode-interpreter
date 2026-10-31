# The WebAssembly build, made with Emscripten (see wasm/README.md):
#   emcmake cmake -S . -B build-wasm
#   cmake --build build-wasm
# It builds the same interpreter twice:
#   PseudocodeInterpreter.js  the command line interpreter, run by Node.js;
#                             build-wasm/pseudocode runs it like the native one
#   pseudocode-core.mjs       a module for web pages, used through
#                             pseudocode.mjs, which runs a program given as text

# Unoptimised WebAssembly is slow and needs far more of the engine's stack
# for each call (see wasm/cli.mjs).
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Errors are C++ exceptions. WebAssembly's own exceptions are much faster
# than Emscripten's JavaScript emulation of them.
target_compile_options(pseudocode_core PUBLIC -fwasm-exceptions)
target_link_options(pseudocode_core PUBLIC -fwasm-exceptions -sALLOW_MEMORY_GROWTH)

# Under Node.js, programs read and write real files and the exit status is
# the interpreter's. Programs run on the calling thread (see Evaluator.cpp),
# so the stack sets how deeply calls can nest: a pseudocode call takes about
# 500 bytes of it, so 64 MB is ample for the default 10,000.
set(PSEUDOCODE_NODE_LINK_OPTIONS -sNODERAWFS -sEXIT_RUNTIME -sENVIRONMENT=node -sSTACK_SIZE=64MB)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE pseudocode_core)
target_link_options(${PROJECT_NAME} PRIVATE ${PSEUDOCODE_NODE_LINK_OPTIONS})

# The launcher runs it in a worker thread with a large stack (wasm/cli.mjs).
# The emulator Emscripten's toolchain file sets is Node.js.
list(GET CMAKE_CROSSCOMPILING_EMULATOR 0 PSEUDOCODE_NODE)
set(PSEUDOCODE_WASM_CLI "${CMAKE_CURRENT_BINARY_DIR}/pseudocode-cli.mjs")
configure_file(wasm/cli.mjs pseudocode-cli.mjs COPYONLY)
# configure_file keeps the template's permissions, and a fresh clone may not
# have it executable, so the launcher is copied into place as executable.
configure_file(wasm/pseudocode.in CMakeFiles/pseudocode @ONLY)
file(COPY "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/pseudocode" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
	FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# For web pages: an ES module whose files are in memory. A page cannot set
# the engine's stack, which runs out first (see wasm/pseudocode.mjs), so a
# smaller stack does.
add_executable(pseudocode_web src/main.cpp)
target_link_libraries(pseudocode_web PRIVATE pseudocode_core)
set_target_properties(pseudocode_web PROPERTIES OUTPUT_NAME pseudocode-core SUFFIX .mjs)
target_link_options(pseudocode_web PRIVATE
	-sMODULARIZE
	-sEXPORT_ES6
	-sINVOKE_RUN=0
	-sEXPORTED_RUNTIME_METHODS=callMain,FS
	-sENVIRONMENT=web,worker,node
	-sSTACK_SIZE=16MB)
configure_file(wasm/pseudocode.mjs pseudocode.mjs COPYONLY)
configure_file(wasm/example.html example.html COPYONLY)
