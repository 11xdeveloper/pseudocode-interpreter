# Tests, run with: ctest --test-dir build

# ---------- unit tests: the core library, called directly ----------

file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")
add_executable(unit_tests ${UNIT_SOURCES})
target_link_libraries(unit_tests PRIVATE pseudocode_core)
if(EMSCRIPTEN)
	# ctest runs it with Node.js, the toolchain's emulator.
	target_link_options(unit_tests PRIVATE ${PSEUDOCODE_NODE_LINK_OPTIONS})
endif()

foreach(suite operations values types environment builtins)
	add_test(NAME unit/${suite} COMMAND unit_tests ${suite})
endforeach()

# ---------- program tests: whole programs run by the interpreter ----------
# Each tests/programs/**/NAME.pseudo is one test, compared with NAME.expected.
# After an intended change in output, rewrite the expected files with
#   cmake --build build --target update_expected
# and review the diff.

# The WebAssembly build cannot start programs, so it runs its interpreter
# (through the build-wasm/pseudocode launcher) with a native build's runner:
#   emcmake cmake -S . -B build-wasm -DPSEUDOCODE_TEST_RUNNER=$PWD/build/tests/program_tests

set(PROGRAMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/programs")
file(GLOB_RECURSE PROGRAM_TESTS CONFIGURE_DEPENDS "${PROGRAMS_DIR}/*.pseudo")

if(EMSCRIPTEN)
	set(PSEUDOCODE_TEST_RUNNER "" CACHE FILEPATH "A native build's program_tests, to run the program tests")
	set(runner "${PSEUDOCODE_TEST_RUNNER}")
	set(interpreter "${PROJECT_BINARY_DIR}/pseudocode")
	if(NOT runner)
		message(STATUS "Program tests skipped: set PSEUDOCODE_TEST_RUNNER to a native build's program_tests")
		set(PROGRAM_TESTS "")
	endif()
else()
	add_executable(program_tests runner/ProgramTests.cpp)
	set(runner program_tests)
	set(interpreter $<TARGET_FILE:PseudocodeInterpreter>)

	add_custom_target(update_expected
		COMMAND program_tests --update --interpreter ${interpreter} "${PROGRAMS_DIR}"
		DEPENDS program_tests PseudocodeInterpreter
		USES_TERMINAL)
endif()

foreach(program ${PROGRAM_TESTS})
	file(RELATIVE_PATH name "${PROGRAMS_DIR}" "${program}")
	string(REGEX REPLACE "\\.pseudo$" "" name "${name}")
	add_test(NAME program/${name} COMMAND ${runner} --interpreter ${interpreter} "${program}")
	set_tests_properties(program/${name} PROPERTIES TIMEOUT 60)
endforeach()
