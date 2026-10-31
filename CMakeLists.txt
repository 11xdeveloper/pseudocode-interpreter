cmake_minimum_required(VERSION 3.15)
project(PseudocodeInterpreter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PSEUDOCODE_BUILD_TESTS "Build the unit and program tests" ON)
option(PSEUDOCODE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(PSEUDOCODE_SANITIZE)
	if(MSVC)
		add_compile_options(/fsanitize=address)
	else()
		add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
		add_link_options(-fsanitize=address,undefined)
	endif()
endif()

# Everything except main.cpp is a library, so the tests can link it too.
# CONFIGURE_DEPENDS re-runs the glob when files are added or removed.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_library(pseudocode_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(pseudocode_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(EMSCRIPTEN)
	include(wasm/Wasm.cmake)
else()
	# The evaluator runs programs on a thread with a large stack.
	find_package(Threads REQUIRED)
	target_link_libraries(pseudocode_core PUBLIC Threads::Threads)

	add_executable(${PROJECT_NAME} src/main.cpp)
	target_link_libraries(${PROJECT_NAME} PRIVATE pseudocode_core)
endif()

if(PSEUDOCODE_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
