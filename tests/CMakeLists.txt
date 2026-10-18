# Tests, run with: ctest --test-dir build

# ---------- unit tests: the core library, called directly ----------

file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")
add_executable(unit_tests ${UNIT_SOURCES})
target_link_libraries(unit_tests PRIVATE pseudocode_core)

foreach(suite operations values types environment builtins)
	add_test(NAME unit/${suite} COMMAND unit_tests ${suite})
endforeach()
