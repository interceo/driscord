# Google WebRTC is built by GN/Ninja, not by CMake.  This module exposes the
# pinned, complete static archive as a normal CMake target without leaking its
# filesystem layout into the rest of the project.

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_driscord_webrtc_windows TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_driscord_webrtc_windows FALSE)
else()
    message(FATAL_ERROR
        "The pinned Google WebRTC artifact is configured for Linux and "
        "Windows x86_64 only.")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _driscord_system_processor)
if(NOT _driscord_system_processor MATCHES "^(x86_64|amd64)$")
    message(FATAL_ERROR
        "The pinned Google WebRTC artifact is built for x86_64, but "
        "CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()
unset(_driscord_system_processor)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Google WebRTC requires a Clang CMake build. Reconfigure this build "
        "with CC=clang CXX=clang++ (or the windows-clang-cl toolchain file) "
        "when DRISCORD_USE_GOOGLE_WEBRTC=ON.")
endif()

if(_driscord_webrtc_windows)
    set(_driscord_webrtc_sdk_dir "google-webrtc-sdk-win")
    set(_driscord_webrtc_out_name "driscord-release-win")
    set(_driscord_webrtc_archive_name "webrtc.lib")
else()
    set(_driscord_webrtc_sdk_dir "google-webrtc-sdk")
    set(_driscord_webrtc_out_name "driscord-release")
    set(_driscord_webrtc_archive_name "libwebrtc.a")
endif()

set(_driscord_webrtc_default_source
    "${CMAKE_SOURCE_DIR}/.cache/google-webrtc/src")
if(EXISTS
    "${CMAKE_SOURCE_DIR}/.cache/${_driscord_webrtc_sdk_dir}/src/api/peer_connection_interface.h")
    set(_driscord_webrtc_default_source
        "${CMAKE_SOURCE_DIR}/.cache/${_driscord_webrtc_sdk_dir}/src")
endif()
# The SDK does not have to live next to the checkout. The CI image bakes it in
# and points DRISCORD_WEBRTC_SDK_ROOT at it — a CI workspace is a fresh clone
# with no .cache/ — and a developer can share one SDK between worktrees the same
# way. An explicit -DDRISCORD_WEBRTC_SOURCE_DIR still wins: the cache entry
# below is only initialised when it is not already set.
if(DEFINED ENV{DRISCORD_WEBRTC_SDK_ROOT} AND EXISTS
    "$ENV{DRISCORD_WEBRTC_SDK_ROOT}/src/api/peer_connection_interface.h")
    set(_driscord_webrtc_default_source "$ENV{DRISCORD_WEBRTC_SDK_ROOT}/src")
endif()
set(DRISCORD_WEBRTC_SOURCE_DIR
    "${_driscord_webrtc_default_source}"
    CACHE PATH "Path to the pinned Google WebRTC source checkout")
set(DRISCORD_WEBRTC_OUT_DIR
    "${DRISCORD_WEBRTC_SOURCE_DIR}/out/${_driscord_webrtc_out_name}"
    CACHE PATH "Path to the Google WebRTC GN output directory")

set(_driscord_webrtc_archive
    "${DRISCORD_WEBRTC_OUT_DIR}/obj/${_driscord_webrtc_archive_name}")

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
if(NOT _driscord_webrtc_windows)
    find_package(X11 REQUIRED)
    find_program(DRISCORD_LLD_LINKER ld.lld REQUIRED)
endif()

# WebRTC's public headers include Abseil as "absl/..." and libyuv as
# "libyuv/...", which the GN build resolves through its own -I flags on those
# third_party directories. Consumers of the archive get no such flags, so the
# same directories have to travel with the imported target.
set(_driscord_webrtc_include_dirs
    "${DRISCORD_WEBRTC_SOURCE_DIR}"
    "${DRISCORD_WEBRTC_SOURCE_DIR}/third_party/abseil-cpp"
    "${DRISCORD_WEBRTC_SOURCE_DIR}/third_party/libyuv/include"
    "${DRISCORD_WEBRTC_OUT_DIR}/gen")

add_library(driscord_google_webrtc STATIC IMPORTED GLOBAL)
add_library(driscord::google_webrtc ALIAS driscord_google_webrtc)
set_target_properties(driscord_google_webrtc PROPERTIES
    IMPORTED_LOCATION "${_driscord_webrtc_archive}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${_driscord_webrtc_include_dirs}"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        "${_driscord_webrtc_include_dirs}"
)

if(_driscord_webrtc_windows)
    # GN compiles the archive's compiler-rt intrinsics against Chromium's
    # clang package; the SDK export carries them so a consumer's clang does
    # not need to ship Windows builtins of its own.
    set(_driscord_webrtc_builtins
        "${DRISCORD_WEBRTC_OUT_DIR}/obj/clang_rt.builtins-x86_64.lib")
    if(NOT EXISTS "${_driscord_webrtc_builtins}")
        message(FATAL_ERROR
            "clang_rt.builtins-x86_64.lib is missing next to the WebRTC "
            "archive. Re-run scripts/build_google_webrtc.sh "
            "(DRISCORD_WEBRTC_TARGET=windows); it exports the intrinsics "
            "library into the SDK.")
    endif()
    set_target_properties(driscord_google_webrtc PROPERTIES
        # Part of the public header ABI, exactly like WEBRTC_USE_X11 below:
        # WEBRTC_WIN selects the platform members of types such as
        # DesktopCaptureOptions.
        INTERFACE_COMPILE_DEFINITIONS
            "WEBRTC_WIN;NOMINMAX;WIN32_LEAN_AND_MEAN"
        # The system libraries GN records for //:webrtc (gn desc ... libs);
        # DEFAULTLIB directives embedded in the archive add the CRT and a few
        # more on top.
        INTERFACE_LINK_LIBRARIES
            "Threads::Threads;${_driscord_webrtc_builtins};crypt32;iphlpapi;secur32;winmm;ole32;oleaut32;strmiids;user32;dmoguids;wmcodecdspuuid;amstrmid;msdmo;d3d11;dxgi;shcore;dwmapi"
    )
    unset(_driscord_webrtc_builtins)
else()
    set_target_properties(driscord_google_webrtc PROPERTIES
        # These definitions are part of the public header ABI, not merely build
        # switches. In particular DesktopCaptureOptions conditionally contains
        # an x_display_ member; omitting WEBRTC_USE_X11 in consumers makes its
        # by-value CreateDefault() return overwrite the caller's stack object.
        INTERFACE_COMPILE_DEFINITIONS
            "WEBRTC_LINUX;WEBRTC_POSIX;WEBRTC_USE_X11"
        # DesktopCapturer is part of the same archive. Its Linux implementation
        # uses these X11 extensions; listing them on the imported target keeps
        # all consumers (including headless tests that only inject frames)
        # linkable.
        INTERFACE_LINK_LIBRARIES
            "Threads::Threads;X11::X11;${X11_Xcomposite_LIB};${X11_Xdamage_LIB};${X11_Xext_LIB};${X11_Xfixes_LIB};${X11_Xrandr_LIB};${X11_Xtst_LIB};${CMAKE_DL_LIBS}"
        # The pinned archive is produced by Chromium's current Clang. GNU ld
        # does not understand every section emitted by that toolchain;
        # matching it with LLVM lld also makes the very large static link
        # substantially faster.
        INTERFACE_LINK_OPTIONS "-fuse-ld=lld"
    )
endif()

unset(_driscord_webrtc_archive)
unset(_driscord_webrtc_archive_name)
unset(_driscord_webrtc_default_source)
unset(_driscord_webrtc_out_name)
unset(_driscord_webrtc_sdk_dir)
unset(_driscord_webrtc_windows)
