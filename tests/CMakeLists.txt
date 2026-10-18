# Tests, run with: ctest --test-dir build

# ---------- unit tests: the core library, called directly ----------

file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")
add_executable(unit_tests ${UNIT_SOURCES})
target_link_libraries(unit_tests PRIVATE pseudocode_core)

foreach(suite operations values types environment builtins)
	add_test(NAME unit/${suite} COMMAND unit_tests ${suite})
endforeach()

# ---------- program tests: whole programs run by the interpreter ----------
# Each tests/programs/**/NAME.pseudo is one test, compared with NAME.expected.
# After an intended change in output, rewrite the expected files with
#   cmake --build build --target update_expected
# and review the diff.

add_executable(program_tests runner/ProgramTests.cpp)

set(PROGRAMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/programs")
file(GLOB_RECURSE PROGRAM_TESTS CONFIGURE_DEPENDS "${PROGRAMS_DIR}/*.pseudo")
foreach(program ${PROGRAM_TESTS})
	file(RELATIVE_PATH name "${PROGRAMS_DIR}" "${program}")
	string(REGEX REPLACE "\\.pseudo$" "" name "${name}")
	add_test(NAME program/${name}
		COMMAND program_tests --interpreter $<TARGET_FILE:PseudocodeInterpreter> "${program}")
	set_tests_properties(program/${name} PROPERTIES TIMEOUT 60)
endforeach()

add_custom_target(update_expected
	COMMAND program_tests --update --interpreter $<TARGET_FILE:PseudocodeInterpreter> "${PROGRAMS_DIR}"
	DEPENDS program_tests PseudocodeInterpreter
	USES_TERMINAL)
