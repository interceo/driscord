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
find_program(CMAKE_OBJDUMP llvm-objdump)

set(CMAKE_C_FLAGS_INIT "/winsysroot${_driscord_msvc_sysroot}")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot${_driscord_msvc_sysroot}")
foreach(_driscord_linker_flags
        CMAKE_EXE_LINKER_FLAGS_INIT
        CMAKE_SHARED_LINKER_FLAGS_INIT
        CMAKE_MODULE_LINKER_FLAGS_INIT)
    set(${_driscord_linker_flags} "/winsysroot:${_driscord_msvc_sysroot}")
endforeach()
unset(_driscord_linker_flags)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

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
