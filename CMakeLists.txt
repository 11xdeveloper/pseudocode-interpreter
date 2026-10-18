cmake_minimum_required(VERSION 3.15)
project(PseudocodeInterpreter VERSION 1.0 LANGUAGES CXX)

# 2. Set the C++ standard to match your original VS settings (e.g., 17 or 20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 3. Find and include all your source files automatically
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h")

# 4. Define the executable name and its sources
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The evaluator runs programs on a thread with a large stack.
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
