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

# ---------- output errors: R015 when output cannot be written ----------
# Writing to /dev/full always fails ("no space left on device"), which a
# program test cannot arrange, so these run the interpreter themselves.

if(EXISTS /dev/full AND NOT WIN32)
	foreach(program final_flush many_lines)
		add_test(NAME output/${program}
			COMMAND sh -c "\"$0\" \"$1\" > /dev/full; echo \"exit status $?\""
				${interpreter} "${CMAKE_CURRENT_SOURCE_DIR}/output/${program}.pseudo")
		set_tests_properties(output/${program} PROPERTIES
			PASS_REGULAR_EXPRESSION "Runtime error R015: The program's output could not be written.*exit status 2"
			TIMEOUT 60)
	endforeach()
endif()

# ---------- package tests: the packed npm package, installed as a user would ----------
# WebAssembly build only, and off by default: they install their tools from
# npm (tests/package/package-lock.json) and need Playwright's Chromium:
#   emcmake cmake -S . -B build-wasm -DPSEUDOCODE_TEST_RUNNER=... -DPSEUDOCODE_PACKAGE_TESTS=ON
#   (cd tests/package && npm ci && npx playwright install chromium)   once, for the browser
#   ctest --test-dir build-wasm -L package

if(EMSCRIPTEN AND TARGET npm_package)
	option(PSEUDOCODE_PACKAGE_TESTS "Test the npm package (installs test tools from npm)" OFF)
endif()
if(PSEUDOCODE_PACKAGE_TESTS AND TARGET npm_package)
	set(package_dir "${CMAKE_CURRENT_SOURCE_DIR}/package")
	set(package_work "${CMAKE_CURRENT_BINARY_DIR}/package")
	set(package_node "${PSEUDOCODE_NODE}")

	add_test(NAME package/install
		COMMAND ${package_node} "${package_dir}/install.mjs" "${PSEUDOCODE_NPM_TARBALL}" "${package_work}")
	set_tests_properties(package/install PROPERTIES FIXTURES_SETUP npm_package TIMEOUT 600)

	add_test(NAME package/node COMMAND ${package_node} --test node.test.mjs WORKING_DIRECTORY "${package_work}")
	add_test(NAME package/types
		COMMAND ${package_node} node_modules/typescript/bin/tsc -p . WORKING_DIRECTORY "${package_work}")
	add_test(NAME package/bundle
		COMMAND ${package_node} node_modules/vite/bin/vite.js build WORKING_DIRECTORY "${package_work}")
	set_tests_properties(package/bundle PROPERTIES FIXTURES_SETUP npm_bundle)
	add_test(NAME package/browser
		COMMAND ${package_node} browser.mjs "${PROGRAMS_DIR}" "${PROJECT_BINARY_DIR}" WORKING_DIRECTORY "${package_work}")
	set_tests_properties(package/browser PROPERTIES FIXTURES_REQUIRED "npm_package;npm_bundle" TIMEOUT 300)
	set(package_tests package/node package/types package/bundle)

	# The program tests again, through the installed `pseudocode` command.
	if(runner AND NOT WIN32)
		add_test(NAME package/programs
			COMMAND ${runner} --interpreter "${package_work}/node_modules/.bin/pseudocode" "${PROGRAMS_DIR}")
		set_tests_properties(package/programs PROPERTIES TIMEOUT 300)
		list(APPEND package_tests package/programs)
	endif()

	set_tests_properties(${package_tests} PROPERTIES FIXTURES_REQUIRED npm_package)
	set_tests_properties(package/install package/browser ${package_tests} PROPERTIES LABELS package)
endif()
