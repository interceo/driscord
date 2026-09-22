
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_driscord_webrtc_platform "windows")
    set(_driscord_webrtc_processors "^(x86_64|amd64)$")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_driscord_webrtc_platform "linux")
    set(_driscord_webrtc_processors "^(x86_64|amd64)$")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_driscord_webrtc_platform "mac")
    set(_driscord_webrtc_processors "^(arm64|aarch64)$")
else()
    message(FATAL_ERROR
        "The pinned Google WebRTC artifact is configured for Linux and "
        "Windows x86_64 and macOS arm64 only.")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _driscord_system_processor)
if(NOT _driscord_system_processor MATCHES "${_driscord_webrtc_processors}")
    message(FATAL_ERROR
        "The pinned Google WebRTC artifact for ${_driscord_webrtc_platform} "
        "does not cover CMAKE_SYSTEM_PROCESSOR '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()
unset(_driscord_system_processor)
unset(_driscord_webrtc_processors)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Google WebRTC requires a Clang CMake build. Reconfigure this build "
        "with CC=clang CXX=clang++ (or the windows-clang-cl toolchain file) "
        "when DRISCORD_USE_GOOGLE_WEBRTC=ON.")
endif()

if(_driscord_webrtc_platform STREQUAL "windows")
    set(_driscord_webrtc_sdk_dir "google-webrtc-sdk-win")
    set(_driscord_webrtc_out_name "driscord-release-win")
    set(_driscord_webrtc_archive_name "webrtc.lib")
elseif(_driscord_webrtc_platform STREQUAL "mac")
    set(_driscord_webrtc_sdk_dir "google-webrtc-sdk-mac")
    set(_driscord_webrtc_out_name "driscord-release-mac")
    set(_driscord_webrtc_archive_name "libwebrtc.a")
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
if(_driscord_webrtc_platform STREQUAL "linux")
    find_package(X11 REQUIRED)
    find_program(DRISCORD_LLD_LINKER ld.lld REQUIRED)
elseif(_driscord_webrtc_platform STREQUAL "mac")
    find_program(DRISCORD_LLD_LINKER ld64.lld REQUIRED)
endif()

set(_driscord_webrtc_include_dirs
    "${DRISCORD_WEBRTC_SOURCE_DIR}"
    "${DRISCORD_WEBRTC_SOURCE_DIR}/third_party/abseil-cpp"
    "${DRISCORD_WEBRTC_SOURCE_DIR}/third_party/libyuv/include"
    "${DRISCORD_WEBRTC_OUT_DIR}/gen")

set(_driscord_boringssl_include
    "${DRISCORD_WEBRTC_SOURCE_DIR}/third_party/boringssl/src/include")
if(NOT EXISTS "${_driscord_boringssl_include}/openssl/curve25519.h")
    message(FATAL_ERROR
        "BoringSSL headers are missing at ${_driscord_boringssl_include}. "
        "The pinned Google WebRTC checkout is expected to bundle them; re-run "
        "scripts/build_google_webrtc.sh.")
endif()
add_library(driscord_boringssl_headers INTERFACE IMPORTED GLOBAL)
add_library(driscord::boringssl_headers ALIAS driscord_boringssl_headers)
set_target_properties(driscord_boringssl_headers PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_driscord_boringssl_include}"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_driscord_boringssl_include}")
unset(_driscord_boringssl_include)

add_library(driscord_google_webrtc STATIC IMPORTED GLOBAL)
add_library(driscord::google_webrtc ALIAS driscord_google_webrtc)
set_target_properties(driscord_google_webrtc PROPERTIES
    IMPORTED_LOCATION "${_driscord_webrtc_archive}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${_driscord_webrtc_include_dirs}"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        "${_driscord_webrtc_include_dirs}"
)

if(_driscord_webrtc_platform STREQUAL "windows")
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
        INTERFACE_COMPILE_DEFINITIONS
            "WEBRTC_WIN;NOMINMAX;WIN32_LEAN_AND_MEAN"
        INTERFACE_LINK_LIBRARIES
            "Threads::Threads;${_driscord_webrtc_builtins};crypt32;iphlpapi;secur32;winmm;ole32;oleaut32;strmiids;user32;dmoguids;wmcodecdspuuid;amstrmid;msdmo;d3d11;dxgi;shcore;dwmapi"
    )
    unset(_driscord_webrtc_builtins)
elseif(_driscord_webrtc_platform STREQUAL "mac")
    # The archive is built by Chromium's clang, whose darwin compiler-rt is not
    # part of a distribution LLVM package; the SDK export carries it along.
    set(_driscord_webrtc_builtins
        "${DRISCORD_WEBRTC_OUT_DIR}/obj/libclang_rt.osx.a")
    if(NOT EXISTS "${_driscord_webrtc_builtins}")
        message(FATAL_ERROR
            "libclang_rt.osx.a is missing next to the WebRTC archive. Re-run "
            "scripts/build_google_webrtc.sh (DRISCORD_WEBRTC_TARGET=mac); it "
            "exports the runtime library into the SDK.")
    endif()
    # The framework set mirrors `gn desc //:webrtc frameworks`; ScreenCaptureKit
    # is weak there because WebRTC resolves it at runtime behind @available.
    set_target_properties(driscord_google_webrtc PROPERTIES
        INTERFACE_COMPILE_DEFINITIONS
            "WEBRTC_MAC;WEBRTC_POSIX"
        INTERFACE_LINK_LIBRARIES
            "Threads::Threads;${_driscord_webrtc_builtins};-framework AppKit;-framework ApplicationServices;-framework AudioToolbox;-framework AVFoundation;-framework CoreAudio;-framework CoreGraphics;-framework CoreMedia;-framework CoreVideo;-framework Foundation;-framework IOKit;-framework IOSurface;-weak_framework ScreenCaptureKit"
        INTERFACE_LINK_OPTIONS "-fuse-ld=lld"
    )
    unset(_driscord_webrtc_builtins)
else()
    set_target_properties(driscord_google_webrtc PROPERTIES
        INTERFACE_COMPILE_DEFINITIONS
            "WEBRTC_LINUX;WEBRTC_POSIX;WEBRTC_USE_X11"
        INTERFACE_LINK_LIBRARIES
            "Threads::Threads;X11::X11;${X11_Xcomposite_LIB};${X11_Xdamage_LIB};${X11_Xext_LIB};${X11_Xfixes_LIB};${X11_Xrandr_LIB};${X11_Xtst_LIB};${CMAKE_DL_LIBS}"
        INTERFACE_LINK_OPTIONS "-fuse-ld=lld"
    )
endif()

unset(_driscord_webrtc_archive)
unset(_driscord_webrtc_archive_name)
unset(_driscord_webrtc_default_source)
unset(_driscord_webrtc_out_name)
unset(_driscord_webrtc_platform)
unset(_driscord_webrtc_sdk_dir)
