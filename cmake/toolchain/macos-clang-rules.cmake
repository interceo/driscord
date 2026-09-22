# Loaded through CMAKE_USER_MAKE_RULES_OVERRIDE, i.e. after CMake's platform
# modules. Platform/Darwin.cmake turns rpath support on only when `sw_vers`
# reports macOS 10.5 or newer, and a Linux host has no sw_vers, while
# CMakeGenericSystem.cmake clears the flag before that -- so the toolchain file
# itself is too early to set it. Without it CMake 3.x rejects every @rpath
# framework the Qt kit imports.
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-rpath,")
