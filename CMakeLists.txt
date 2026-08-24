cmake_minimum_required(VERSION 3.25)
# CMake 3.30+ prefers Boost's config package. Hosted Ubuntu 24.04 runners ship
# 3.28, where this policy does not exist and setting it unconditionally makes
# configuration fail before the first target is seen.
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

include(cmake/Version.cmake)
project(driscord VERSION ${DRISCORD_VERSION_CORE} LANGUAGES C CXX)

configure_file(cmake/version.hpp.in
    "${CMAKE_BINARY_DIR}/generated/driscord/version.hpp" @ONLY)
message(STATUS "Driscord version: ${DRISCORD_VERSION_DISPLAY}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_CORE "Build the core library" ON)
option(BUILD_SERVER "Build the signaling server" ON)

# libFuzzer harnesses over the SFU's parsing/rewrite surface (Clang only).
# Instruments every target in the tree with fuzzer coverage + ASan/UBSan so
# the fuzzers see through driscord_signaling and libdatachannel alike; the
# fuzz executables themselves add -fsanitize=fuzzer for the driver.
option(DRISCORD_FUZZ "Build libFuzzer fuzz targets (requires Clang)" OFF)

# Standalone sanitizer switch for ordinary test runs: a comma-separated
# -fsanitize list, e.g. "address,undefined". Empty disables.
set(DRISCORD_SANITIZE "" CACHE STRING
    "Comma-separated -fsanitize= list applied to the whole tree")

# llvm-cov source-based coverage instrumentation for the whole tree.
option(DRISCORD_COVERAGE
    "Instrument with -fprofile-instr-generate/-fcoverage-mapping" OFF)

if(DRISCORD_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DRISCORD_FUZZ requires Clang (libFuzzer)")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined
        -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
elseif(DRISCORD_SANITIZE)
    add_compile_options(-fsanitize=${DRISCORD_SANITIZE}
        -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${DRISCORD_SANITIZE})
endif()

if(DRISCORD_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DRISCORD_COVERAGE requires Clang (llvm-cov)")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

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
    PATCH_COMMAND
        "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_SOURCE_DIR}/third_party/patches/libdatachannel-gnutls-partial-write.patch"
        -P "${CMAKE_SOURCE_DIR}/cmake/ApplyGitPatch.cmake"
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
    set(DRISCORD_CLIENT_SIGNALING_URL "ws://localhost:9001" CACHE STRING
        "WebSocket base URL compiled into the Driscord client")
    set(DRISCORD_CLIENT_API_URL "http://localhost:9002" CACHE STRING
        "HTTP base URL compiled into the Driscord client")

    if(NOT DRISCORD_CLIENT_SIGNALING_URL MATCHES "^wss?://[^/]+")
        message(FATAL_ERROR
            "DRISCORD_CLIENT_SIGNALING_URL must be an absolute ws:// or wss:// URL")
    endif()
    if(NOT DRISCORD_CLIENT_API_URL MATCHES "^https?://[^/]+")
        message(FATAL_ERROR
            "DRISCORD_CLIENT_API_URL must be an absolute http:// or https:// URL")
    endif()
    foreach(_driscord_client_url IN ITEMS
            DRISCORD_CLIENT_SIGNALING_URL DRISCORD_CLIENT_API_URL)
        if(${_driscord_client_url} MATCHES "[\\\"]")
            message(FATAL_ERROR
                "${_driscord_client_url} contains a character that is invalid in a URL")
        endif()
    endforeach()
    unset(_driscord_client_url)

    string(REGEX REPLACE "/+$" "" DRISCORD_CLIENT_SIGNALING_URL_VALUE
        "${DRISCORD_CLIENT_SIGNALING_URL}")
    string(REGEX REPLACE "/+$" "" DRISCORD_CLIENT_API_URL_VALUE
        "${DRISCORD_CLIENT_API_URL}")
    configure_file(cmake/client_build_config.hpp.in
        "${CMAKE_BINARY_DIR}/generated/driscord/client_build_config.hpp" @ONLY)
    message(STATUS
        "Driscord client endpoints: ${DRISCORD_CLIENT_SIGNALING_URL_VALUE}, "
        "${DRISCORD_CLIENT_API_URL_VALUE}")

    add_subdirectory(client-qt)
endif()

option(DRISCORD_ENABLE_PACKAGING "Create a portable client package" OFF)
if(DRISCORD_ENABLE_PACKAGING)
    if(NOT BUILD_QT_CLIENT)
        message(FATAL_ERROR
            "DRISCORD_ENABLE_PACKAGING requires BUILD_QT_CLIENT=ON")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
        OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        message(FATAL_ERROR
            "The release package currently supports Linux x86_64 only")
    endif()

    set(_driscord_default_package_dir "${CMAKE_BINARY_DIR}/packages")
    if(DEFINED ENV{DRISCORD_RELEASE_STAGING}
        AND NOT "$ENV{DRISCORD_RELEASE_STAGING}" STREQUAL "")
        set(_driscord_default_package_dir "$ENV{DRISCORD_RELEASE_STAGING}")
    endif()
    set(DRISCORD_RELEASE_STAGING "${_driscord_default_package_dir}"
        CACHE PATH "Directory for generated release artifacts")

    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_NAME "driscord-client")
    set(CPACK_PACKAGE_VENDOR "Driscord")
    set(CPACK_PACKAGE_VERSION "${DRISCORD_VERSION_DISPLAY}")
    set(CPACK_PACKAGE_FILE_NAME
        "driscord-client-${DRISCORD_VERSION_DISPLAY}-linux-x86_64")
    set(CPACK_PACKAGE_DIRECTORY "${DRISCORD_RELEASE_STAGING}")
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    # CPack otherwise switches back to a monolithic install when only one
    # component is selected. That pulls libdatachannel's headers and CMake
    # package metadata from its implicit Unspecified component into the TGZ.
    set(CPACK_MONOLITHIC_INSTALL OFF)
    set(CPACK_COMPONENTS_ALL Runtime)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    set(CPACK_ARCHIVE_FILE_NAME
        "driscord-client-${DRISCORD_VERSION_DISPLAY}-linux-x86_64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PRE_BUILD_SCRIPTS
        "${CMAKE_SOURCE_DIR}/cmake/VerifyPackage.cmake")
    include(CPack)
    unset(_driscord_default_package_dir)
endif()
