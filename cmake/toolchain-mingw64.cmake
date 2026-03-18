# Cross-compilation toolchain: Linux → Windows x64 via MinGW-w64
#
# Install toolchain (Arch):
#   sudo pacman -S mingw-w64-gcc mingw-w64-openssl
#
# Usage:
#   cmake -S . -B build-win \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DCMAKE_BUILD_TYPE=Release

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ---------------------------------------------------------------------------
# Compiler selection — prefer posix threading model for std::thread support
# ---------------------------------------------------------------------------
find_program(MINGW_C   NAMES x86_64-w64-mingw32-gcc-posix   x86_64-w64-mingw32-gcc   REQUIRED)
find_program(MINGW_CXX NAMES x86_64-w64-mingw32-g++-posix   x86_64-w64-mingw32-g++   REQUIRED)
find_program(MINGW_RC  NAMES x86_64-w64-mingw32-windres                                REQUIRED)

set(CMAKE_C_COMPILER   ${MINGW_C})
set(CMAKE_CXX_COMPILER ${MINGW_CXX})
set(CMAKE_RC_COMPILER  ${MINGW_RC})
set(CMAKE_AR           x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB       x86_64-w64-mingw32-ranlib)
set(CMAKE_STRIP        x86_64-w64-mingw32-strip)

# ---------------------------------------------------------------------------
# Sysroot & library search
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# OpenSSL for the Windows target — install: sudo pacman -S mingw-w64-openssl
set(OPENSSL_ROOT_DIR        "/usr/x86_64-w64-mingw32" CACHE PATH "")
set(OPENSSL_USE_STATIC_LIBS TRUE                      CACHE BOOL "")

# ---------------------------------------------------------------------------
# Linker: statically embed MinGW runtime (no extra DLLs to ship)
# ---------------------------------------------------------------------------
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LIBRARY_SUFFIX    ".dll")

# ---------------------------------------------------------------------------
# Compiler flags — applied to ALL targets via CMAKE_{C,CXX}_FLAGS_INIT so
# they reliably propagate into FetchContent dependencies too.
#
# We also force-include cmake/mingw64_fixup.h which:
#   1. Overrides __UINT64_TYPE__ / __INT64_TYPE__ etc. to 'long long' before
#      <stdint.h> typedef-s them — on LLP64 Windows 'unsigned long' is 32-bit,
#      so without this fix uint64_t ends up as a 32-bit type.
#   2. Includes <wchar.h>, <string.h>, <stdio.h> early so that _wcsicmp /
#      _wfopen are declared before stralign.h / miniaudio.h use them.
# ---------------------------------------------------------------------------
set(_MINGW_FIXUP "${CMAKE_CURRENT_LIST_DIR}/mingw64_fixup.h")

set(_COMMON_FLAGS
    # Suppress redefinition warnings from our __UINT64_TYPE__ override
    "-Wno-builtin-macro-redefined"
    # GCC 15 stricter template-body checking hits system headers
    "-Wno-template-body"
    # SI-prefix macros (tera/peta/exa…) from Windows SDK collide with <chrono>
    "-Umilli -Umicro -Unano -Upico -Ufemto -Uatto"
    "-Ukilo  -Umega  -Ugiga -Utera -Upeta  -Uexa"
    # std::ratio constants don't fit in intmax_t=long (32-bit on LLP64)
    "-Wno-narrowing"
    # (1<<N) N≥32 appears in system <chrono> and nlohmann/json headers
    "-Wno-shift-count-overflow"
    # Force-include fixup header (absolute path, no spaces)
    "-include ${_MINGW_FIXUP}"
)
list(JOIN _COMMON_FLAGS " " _COMMON_FLAGS_STR)

set(CMAKE_C_FLAGS_INIT   "${_COMMON_FLAGS_STR}")
set(CMAKE_CXX_FLAGS_INIT "${_COMMON_FLAGS_STR} -fpermissive")

# ---------------------------------------------------------------------------
# Preprocessor definitions
# ---------------------------------------------------------------------------
add_compile_definitions(
    WIN32_LEAN_AND_MEAN   # skip bloated Windows headers that pollute macros
)
# NOMINMAX only for C++ — C code (usrsctp) relies on min/max being macros
add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
