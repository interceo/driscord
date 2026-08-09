# Google WebRTC is built by GN/Ninja, not by CMake.  This module exposes the
# pinned, complete static archive as a normal CMake target without leaking its
# filesystem layout into the rest of the project.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "The pinned Google WebRTC artifact is currently configured for Linux only.")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _driscord_system_processor)
if(NOT _driscord_system_processor MATCHES "^(x86_64|amd64)$")
    message(FATAL_ERROR
        "The pinned Google WebRTC artifact is built for Linux x86_64, but "
        "CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()
unset(_driscord_system_processor)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Google WebRTC requires a Clang CMake build. Reconfigure this build "
        "with CC=clang CXX=clang++ when DRISCORD_USE_GOOGLE_WEBRTC=ON.")
endif()

set(DRISCORD_WEBRTC_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/.cache/google-webrtc/src"
    CACHE PATH "Path to the pinned Google WebRTC source checkout")
set(DRISCORD_WEBRTC_OUT_DIR
    "${DRISCORD_WEBRTC_SOURCE_DIR}/out/driscord-release"
    CACHE PATH "Path to the Google WebRTC GN output directory")

set(_driscord_webrtc_archive
    "${DRISCORD_WEBRTC_OUT_DIR}/obj/libwebrtc.a")

if(NOT EXISTS "${DRISCORD_WEBRTC_SOURCE_DIR}/api/peer_connection_interface.h")
    message(FATAL_ERROR
        "Google WebRTC checkout is missing at ${DRISCORD_WEBRTC_SOURCE_DIR}. "
        "Run scripts/build_google_webrtc.sh first.")
endif()

if(NOT EXISTS "${_driscord_webrtc_archive}")
    message(FATAL_ERROR
        "Google WebRTC archive is missing at ${_driscord_webrtc_archive}. "
        "Run scripts/build_google_webrtc.sh first.")
endif()

find_package(Threads REQUIRED)
find_package(X11 REQUIRED)
find_program(DRISCORD_LLD_LINKER ld.lld REQUIRED)

add_library(driscord_google_webrtc STATIC IMPORTED GLOBAL)
add_library(driscord::google_webrtc ALIAS driscord_google_webrtc)
set_target_properties(driscord_google_webrtc PROPERTIES
    IMPORTED_LOCATION "${_driscord_webrtc_archive}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${DRISCORD_WEBRTC_SOURCE_DIR};${DRISCORD_WEBRTC_OUT_DIR}/gen"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        "${DRISCORD_WEBRTC_SOURCE_DIR};${DRISCORD_WEBRTC_OUT_DIR}/gen"
    INTERFACE_COMPILE_DEFINITIONS "WEBRTC_LINUX;WEBRTC_POSIX"
    # DesktopCapturer is part of the same archive. Its Linux implementation
    # uses these X11 extensions; listing them on the imported target keeps all
    # consumers (including headless tests that only inject frames) linkable.
    INTERFACE_LINK_LIBRARIES
        "Threads::Threads;X11::X11;${X11_Xcomposite_LIB};${X11_Xdamage_LIB};${X11_Xext_LIB};${X11_Xfixes_LIB};${X11_Xrandr_LIB};${X11_Xtst_LIB};${CMAKE_DL_LIBS}"
    # The pinned archive is produced by Chromium's current Clang. GNU ld does
    # not understand every section emitted by that toolchain; matching it with
    # LLVM lld also makes the very large static link substantially faster.
    INTERFACE_LINK_OPTIONS "-fuse-ld=lld"
)

unset(_driscord_webrtc_archive)
