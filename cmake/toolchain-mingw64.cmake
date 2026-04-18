# MinGW-w64 cross-compilation toolchain (Linux host → Windows x86_64 target).
#
# Before the first build, run:
#   ./third_party/fetch_win_deps.sh
#
# Usage:
#   cmake -S . -B build-win \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#       -DCMAKE_BUILD_TYPE=Release \
#       -DBUILD_SERVER=OFF

cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ---------------------------------------------------------------------------
# Compiler — prefer POSIX threading variant (needed for std::thread/std::mutex)
# ---------------------------------------------------------------------------
find_program(_MINGW_CXX NAMES
    x86_64-w64-mingw32-g++-posix
    x86_64-w64-mingw32-g++
)
find_program(_MINGW_CC NAMES
    x86_64-w64-mingw32-gcc-posix
    x86_64-w64-mingw32-gcc
)
find_program(_MINGW_RC NAMES x86_64-w64-mingw32-windres)

if(NOT _MINGW_CXX)
    message(FATAL_ERROR
        "x86_64-w64-mingw32-g++ not found.\n"
        "Install: pacman -S mingw-w64-gcc")
endif()

set(CMAKE_C_COMPILER   "${_MINGW_CC}")
set(CMAKE_CXX_COMPILER "${_MINGW_CXX}")
set(CMAKE_RC_COMPILER  "${_MINGW_RC}")

# ---------------------------------------------------------------------------
# Search roots — third_party prebuilts take priority over the system sysroot
# ---------------------------------------------------------------------------
set(MINGW_SYSROOT /usr/x86_64-w64-mingw32)

# cmake/ is CMAKE_CURRENT_LIST_DIR; parent = project root
get_filename_component(_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_TP "${_ROOT}/third_party/windows")

list(APPEND CMAKE_FIND_ROOT_PATH "${MINGW_SYSROOT}")

# ---------------------------------------------------------------------------
# FFmpeg (third_party/windows/ffmpeg/)
# ---------------------------------------------------------------------------
set(_FF "${_TP}/ffmpeg")
if(EXISTS "${_FF}/include" AND EXISTS "${_FF}/lib")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${_FF}")
    list(PREPEND CMAKE_PREFIX_PATH    "${_FF}")
    message(STATUS "[toolchain] FFmpeg: ${_FF}")
else()
    message(STATUS "[toolchain] third_party FFmpeg not found — run third_party/fetch_win_deps.sh")
endif()

# ---------------------------------------------------------------------------
# OpenSSL (third_party/windows/openssl/)
# FindOpenSSL sometimes ignores CMAKE_FIND_ROOT_PATH, so set the cache vars
# explicitly — this guarantees find_package(OpenSSL REQUIRED) succeeds.
# ---------------------------------------------------------------------------
set(_SSL "${_TP}/openssl")
if(EXISTS "${_SSL}/include" AND EXISTS "${_SSL}/lib")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${_SSL}")
    set(OPENSSL_ROOT_DIR     "${_SSL}"                      CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR  "${_SSL}/include"              CACHE PATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY      "${_SSL}/lib/libssl.a"     CACHE FILEPATH "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY   "${_SSL}/lib/libcrypto.a"  CACHE FILEPATH "" FORCE)
    message(STATUS "[toolchain] OpenSSL: ${_SSL}")
else()
    # Fall back to system MinGW sysroot (mingw-w64-openssl from pacman)
    if(EXISTS "${MINGW_SYSROOT}/lib/libssl.a")
        set(OPENSSL_ROOT_DIR    "${MINGW_SYSROOT}"                          CACHE PATH "" FORCE)
        set(OPENSSL_INCLUDE_DIR "${MINGW_SYSROOT}/include"                  CACHE PATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY     "${MINGW_SYSROOT}/lib/libssl.a"         CACHE FILEPATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY  "${MINGW_SYSROOT}/lib/libcrypto.a"      CACHE FILEPATH "" FORCE)
        message(STATUS "[toolchain] OpenSSL: system sysroot (${MINGW_SYSROOT})")
    else()
        message(WARNING
            "[toolchain] OpenSSL not found.\n"
            "Run: ./third_party/fetch_win_deps.sh\n"
            "  or: pacman -S mingw-w64-openssl")
    endif()
endif()

# ---------------------------------------------------------------------------
# Search mode — never look at host system libraries/headers
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
