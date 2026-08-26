# Cross toolchain: Linux host -> Windows x64 with clang-cl against the packaged
# MSVC sysroot. DRISCORD_MSVC_SYSROOT is the same tree
# scripts/build_google_webrtc.sh consumes (xwin splat --use-winsysroot-style
# plus the SetEnv/cl.exe finishing step); clang, lld-link and the WebRTC GN
# build therefore share one sysroot pin. Tools resolve through PATH: clang-cl,
# lld-link, llvm-lib, llvm-rc and llvm-mt of one LLVM major.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if("$ENV{DRISCORD_MSVC_SYSROOT}" STREQUAL "")
    message(FATAL_ERROR
        "The Windows cross build needs DRISCORD_MSVC_SYSROOT in the "
        "environment (packaged MSVC+SDK layout).")
endif()
file(TO_CMAKE_PATH "$ENV{DRISCORD_MSVC_SYSROOT}" _driscord_msvc_sysroot)
if(NOT EXISTS "${_driscord_msvc_sysroot}/VC/Tools/MSVC")
    message(FATAL_ERROR
        "DRISCORD_MSVC_SYSROOT=${_driscord_msvc_sysroot} does not look like "
        "an MSVC sysroot (VC/Tools/MSVC is missing).")
endif()
if(_driscord_msvc_sysroot MATCHES " ")
    # The flag below travels unquoted through *_FLAGS_INIT.
    message(FATAL_ERROR "DRISCORD_MSVC_SYSROOT must not contain spaces")
endif()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
# The packaging closure reads PE import tables through
# file(GET_RUNTIME_DEPENDENCIES); pin the reader to LLVM instead of hoping
# the host binutils was built with PE support.
find_program(CMAKE_OBJDUMP llvm-objdump)

# Plain set, not append: CMake re-reads the toolchain file for every language
# and try_compile, and appending would stack the flag.
set(CMAKE_C_FLAGS_INIT "/winsysroot${_driscord_msvc_sysroot}")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot${_driscord_msvc_sysroot}")
foreach(_driscord_linker_flags
        CMAKE_EXE_LINKER_FLAGS_INIT
        CMAKE_SHARED_LINKER_FLAGS_INIT
        CMAKE_MODULE_LINKER_FLAGS_INIT)
    set(${_driscord_linker_flags} "/winsysroot:${_driscord_msvc_sysroot}")
endforeach()
unset(_driscord_linker_flags)

# The sysroot deliberately carries neither the dynamic nor the debug CRT
# (xwin default); every configuration of this toolchain uses the static
# release CRT, matching the /MT GN build of the WebRTC archive.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Target-platform dependencies (Qt msvc2019_64, Boost headers) come from the
# environment like every other preset input; host programs stay findable.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
foreach(_driscord_root "$ENV{DRISCORD_QT_WIN_ROOT}" "$ENV{BOOST_ROOT}")
    if(NOT _driscord_root STREQUAL "")
        file(TO_CMAKE_PATH "${_driscord_root}" _driscord_root)
        list(APPEND CMAKE_FIND_ROOT_PATH "${_driscord_root}")
    endif()
endforeach()
unset(_driscord_root)
unset(_driscord_msvc_sysroot)
