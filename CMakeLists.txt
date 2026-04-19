cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(driscord VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

include(FetchContent)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fmt)

# --- nlohmann/json (shared by core + signaling server) ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

# The signaling server is Linux-only; disable when cross-compiling for Windows.
option(BUILD_SERVER "Build the signaling server" ON)
if(BUILD_SERVER)
    add_subdirectory(backend/signaling_server)
endif()

option(BUILD_CORE "Build the core library" ON)
if(BUILD_CORE)
    add_subdirectory(core)
endif()

option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(core/benchmarks)
endif()

option(BUILD_LAUNCHER "Build the Windows EXE launcher" OFF)
if(BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()
