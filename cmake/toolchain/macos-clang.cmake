set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
# The Darwin kernel of the deployment target below (macOS 13); a cross build
# has no uname to report it.
set(CMAKE_SYSTEM_VERSION 22.0)
set(CMAKE_USER_MAKE_RULES_OVERRIDE
    "${CMAKE_CURRENT_LIST_DIR}/macos-clang-rules.cmake")

if("$ENV{DRISCORD_MACOS_SDK}" STREQUAL "")
    message(FATAL_ERROR
        "The macOS cross build needs DRISCORD_MACOS_SDK in the environment "
        "(an extracted MacOSX<version>.sdk directory).")
endif()
file(TO_CMAKE_PATH "$ENV{DRISCORD_MACOS_SDK}" _driscord_macos_sdk)
if(NOT EXISTS "${_driscord_macos_sdk}/SDKSettings.json")
    message(FATAL_ERROR
        "DRISCORD_MACOS_SDK=${_driscord_macos_sdk} does not look like a macOS "
        "SDK (SDKSettings.json is missing).")
endif()

# At run time the C++ library is the system's libc++.dylib, so the headers must
# be the SDK's. clang's Darwin driver prefers a libc++ found next to the
# compiler whenever one exists (LLVM release tarballs and libc++-dev packages
# ship one), which would compile against a libc++ the app never loads.
foreach(_driscord_lang CXX OBJCXX)
    string(APPEND CMAKE_${_driscord_lang}_FLAGS_INIT
        " -stdlib++-isystem ${_driscord_macos_sdk}/usr/include/c++/v1")
endforeach()
unset(_driscord_lang)

# An absolute sysroot keeps CMake's Darwin platform module from shelling out to
# xcrun to discover one, which is what it does when this is left unset.
set(CMAKE_OSX_SYSROOT "${_driscord_macos_sdk}" CACHE PATH "" FORCE)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
# Must match mac_deployment_target in the pinned WebRTC build, or weak-linked
# symbols resolve differently in the archive than in the code calling it.
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "" FORCE)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET arm64-apple-macos13)
set(CMAKE_CXX_COMPILER_TARGET arm64-apple-macos13)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_STRIP llvm-strip)
set(CMAKE_INSTALL_NAME_TOOL llvm-install-name-tool)
find_program(CMAKE_OBJDUMP llvm-objdump)
find_program(DRISCORD_OTOOL llvm-otool)
find_program(DRISCORD_LIPO llvm-lipo)

# clang only drives ld64.lld correctly when it resolves the linker by name, so
# the toolchain asks for `lld` rather than an absolute path.
foreach(_driscord_linker_flags
        CMAKE_EXE_LINKER_FLAGS_INIT
        CMAKE_SHARED_LINKER_FLAGS_INIT
        CMAKE_MODULE_LINKER_FLAGS_INIT)
    set(${_driscord_linker_flags} "-fuse-ld=lld")
endforeach()
unset(_driscord_linker_flags)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
list(APPEND CMAKE_FIND_ROOT_PATH "${_driscord_macos_sdk}")
foreach(_driscord_root "$ENV{DRISCORD_QT_MAC_ROOT}" "$ENV{BOOST_ROOT}")
    if(NOT _driscord_root STREQUAL "")
        file(TO_CMAKE_PATH "${_driscord_root}" _driscord_root)
        list(APPEND CMAKE_FIND_ROOT_PATH "${_driscord_root}")
    endif()
endforeach()
unset(_driscord_root)
unset(_driscord_macos_sdk)
