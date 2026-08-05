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

# --- libdatachannel (shared by core + signaling server) ---
# The client uses rtc::WebSocket for signaling and one rtc::PeerConnection to
# the server; the server terminates a PeerConnection per client (SFU), so both
# targets need this. Declared here because the server subdirectory is added
# before core/ and is also built standalone with -DBUILD_CORE=OFF.
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.22.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libdatachannel)

# --- boost-ext/SML (shared by core + signaling server) ---
# Connection lifecycle state machines: one for the client's link to the server
# (core/src/transport_fsm.hpp) and one per server-side session
# (backend/signaling_server/src/session_fsm.hpp). Header-only.
set(SML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SML_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    sml
    GIT_REPOSITORY https://github.com/boost-ext/sml.git
    GIT_TAG        v1.1.13
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sml)

# Treat SML's header as a system include: instantiating its templates under
# GCC 16 trips -Wmaybe-uninitialized inside the library itself, which is noise
# we can neither fix nor act on.
get_target_property(_sml_include_dirs sml INTERFACE_INCLUDE_DIRECTORIES)
if(_sml_include_dirs)
    set_target_properties(sml PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_sml_include_dirs}")
endif()

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

option(BUILD_QT_CLIENT "Build the Qt/QML client" OFF)
if(BUILD_QT_CLIENT)
    add_subdirectory(client-qt)
endif()
