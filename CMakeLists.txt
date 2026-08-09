cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(driscord VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_CORE "Build the core library" ON)
option(BUILD_SERVER "Build the signaling server" ON)

# Google WebRTC is the only client media backend. Server-only builds remain
# independent of the external GN artifact by configuring BUILD_CORE=OFF.
option(DRISCORD_USE_GOOGLE_WEBRTC
    "Build the Google WebRTC client media backend" ON)
if(BUILD_CORE AND NOT DRISCORD_USE_GOOGLE_WEBRTC)
    message(FATAL_ERROR
        "The legacy media backend was removed; BUILD_CORE requires "
        "DRISCORD_USE_GOOGLE_WEBRTC=ON.")
endif()
if(BUILD_CORE)
    include(cmake/GoogleWebRTC.cmake)
endif()

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
    GIT_TAG        12.2.0
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

# --- libdatachannel (WebSocket client + server-side Track SFU) ---
option(DRISCORD_ENABLE_RTP_MEDIA
    "Build libdatachannel RTP/RTCP media support for the SFU" ON)
if(BUILD_SERVER AND DRISCORD_ENABLE_RTP_MEDIA)
    set(NO_MEDIA OFF CACHE BOOL "" FORCE)
else()
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
endif()
# BoringSSL in Google's complete static archive intentionally exports the same
# C symbols as OpenSSL. The Qt client still uses libdatachannel's WebSocket, so
# its DSO is built with GnuTLS to prevent TLS symbol interposition. A standalone
# server build remains on libdatachannel's default OpenSSL backend.
if(BUILD_CORE)
    set(USE_GNUTLS ON CACHE BOOL "Use GnuTLS instead of OpenSSL" FORCE)
    set(USE_MBEDTLS OFF CACHE BOOL "Use Mbed TLS instead of OpenSSL" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL
        "Disable OpenSSL in bundled libSRTP for BoringSSL coexistence" FORCE)
    # GnuTLS handles DTLS in libdatachannel, but bundled libSRTP still needs
    # an external AES-GCM implementation. NSS is selected only for that nested
    # library; USE_MBEDTLS above remains OFF.
    set(ENABLE_MBEDTLS OFF CACHE BOOL
        "Disable Mbed TLS crypto in bundled libSRTP" FORCE)
    set(ENABLE_NSS ON CACHE BOOL
        "Use NSS crypto in bundled libSRTP" FORCE)
    # libSRTP stores the selected engine in INTERNAL cache variables but does
    # not clear a previously selected one when a build directory is
    # reconfigured. Avoid an impossible OPENSSL+NSS config after migration.
    unset(OPENSSL CACHE)
    unset(MBEDTLS CACHE)
else()
    set(USE_GNUTLS OFF CACHE BOOL "Use GnuTLS instead of OpenSSL" FORCE)
    set(ENABLE_OPENSSL ON CACHE BOOL
        "Use OpenSSL crypto in bundled libSRTP" FORCE)
    set(ENABLE_NSS OFF CACHE BOOL
        "Disable NSS crypto in bundled libSRTP" FORCE)
    unset(NSS CACHE)
    unset(MBEDTLS CACHE)
endif()
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.24.5
    GIT_SHALLOW    TRUE
)
# In the client process libdatachannel must be a shared isolation boundary.
# Both projects embed libSRTP and intentionally expose the same C
# API; linking both static archives into one executable either produces
# duplicate definitions or, depending on archive order, silently mixes their
# process-global state. Keep libdatachannel's bundled dependencies local to
# its DSO below. Other builds retain the project's explicit static target.
if(BUILD_CORE)
    set(_driscord_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS ON)
endif()
FetchContent_MakeAvailable(libdatachannel)
if(BUILD_CORE)
    set(BUILD_SHARED_LIBS "${_driscord_saved_build_shared_libs}")
    unset(_driscord_saved_build_shared_libs)
    set(DRISCORD_LIBDATACHANNEL_TARGET datachannel)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Make libSRTP/usrsctp/libjuice symbols from dependency archives local
        # to libdatachannel.so. Its annotated public rtc::* API remains
        # exported, while Google WebRTC keeps its own private media stack.
        target_link_options(datachannel PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
else()
    set(DRISCORD_LIBDATACHANNEL_TARGET datachannel-static)
endif()

# The non-WebRTC build links the explicit static target everywhere. Upstream's
# shared target would otherwise compile the stack a second time unused.
if(TARGET datachannel AND NOT BUILD_CORE)
    set_target_properties(datachannel PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# --- Boost (header-only, shared by protocol + core + signaling server) ---
# Lock-free/bounded queues are used by hardware adapters; ASIO/Beast powers the
# signaling server.
find_package(Boost 1.89 REQUIRED)

# Shared wire/signaling protocol code used by both the client core and the
# signaling server. Keep implementations out of headers so protocol changes
# compile as normal translation units instead of spreading inline code through
# every consumer.
add_library(driscord_protocol STATIC
    core/src/utils/signaling_protocol.cpp
)
target_include_directories(driscord_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/core/src
    ${CMAKE_SOURCE_DIR}/core/src/utils
)
target_link_libraries(driscord_protocol PUBLIC
    nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(driscord_protocol PRIVATE /W4)
else()
    target_compile_options(driscord_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The signaling server is Linux-only; disable when cross-compiling for Windows.
if(BUILD_SERVER)
    add_subdirectory(backend/signaling_server)
endif()

if(BUILD_CORE)
    add_subdirectory(core)
endif()

option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

option(BUILD_QT_CLIENT "Build the Qt/QML client" OFF)
if(BUILD_QT_CLIENT)
    add_subdirectory(client-qt)
endif()
