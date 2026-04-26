cmake_minimum_required(VERSION 3.20)

project(driscord_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# --- libdatachannel (WebRTC + WebSocket) ---
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.22.5
    GIT_SHALLOW    TRUE
)

# --- Opus ---
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.5.2
    GIT_SHALLOW    TRUE
)

# --- miniaudio (header-only) ---
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw libdatachannel opus miniaudio)

# miniaudio interface library
add_library(miniaudio_lib INTERFACE)
target_include_directories(miniaudio_lib INTERFACE ${miniaudio_SOURCE_DIR})

# --- FFmpeg ---
# When cross-compiling for Windows, prefer prebuilt static libs from
# third_party/windows/ffmpeg/ (populated by third_party/fetch_win_deps.sh).
# The toolchain file already adds that path to CMAKE_PREFIX_PATH/FIND_ROOT_PATH,
# so find_path/find_library will pick it up automatically via the fallback below.
if(CMAKE_CROSSCOMPILING AND WIN32)
    set(_WIN_FFMPEG "${CMAKE_SOURCE_DIR}/third_party/windows/ffmpeg")
    if(EXISTS "${_WIN_FFMPEG}/include" AND EXISTS "${_WIN_FFMPEG}/lib")
        set(_FF_INC "${_WIN_FFMPEG}/include")
        set(_FF_LIB "${_WIN_FFMPEG}/lib")
        add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
        set_target_properties(PkgConfig::FFMPEG PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_FF_INC}"
            INTERFACE_LINK_LIBRARIES
                "${_FF_LIB}/libavcodec.dll.a;\
${_FF_LIB}/libavformat.dll.a;\
${_FF_LIB}/libavdevice.dll.a;\
${_FF_LIB}/libswscale.dll.a;\
${_FF_LIB}/libswresample.dll.a;\
${_FF_LIB}/libavutil.dll.a"
        )
        message(STATUS "FFmpeg (Windows cross, shared): ${_WIN_FFMPEG}")
    endif()
endif()

find_package(PkgConfig QUIET)
if(NOT TARGET PkgConfig::FFMPEG AND PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavcodec libavformat libavdevice libswscale libswresample libavutil)
endif()

if(NOT TARGET PkgConfig::FFMPEG)
    find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
    find_library(AVCODEC_LIBRARY avcodec)
    find_library(AVFORMAT_LIBRARY avformat)
    find_library(AVDEVICE_LIBRARY avdevice)
    find_library(SWSCALE_LIBRARY swscale)
    find_library(SWRESAMPLE_LIBRARY swresample)
    find_library(AVUTIL_LIBRARY avutil)

    if(NOT AVCODEC_LIBRARY)
        message(FATAL_ERROR
            "FFmpeg not found. Install via system package manager, vcpkg, "
            "or set CMAKE_PREFIX_PATH to the FFmpeg install directory.")
    endif()

    add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
    set_target_properties(PkgConfig::FFMPEG PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${AVCODEC_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES
            "${AVCODEC_LIBRARY};${AVFORMAT_LIBRARY};${AVDEVICE_LIBRARY};${SWSCALE_LIBRARY};${SWRESAMPLE_LIBRARY};${AVUTIL_LIBRARY}"
    )
endif()

# ---------------------------------------------------------------------------
# Prefer static FFmpeg so libcore.so / core.dll is self-contained.
# On Linux the JNI output is a shared object — system FFmpeg .a libs are not
# built with -fPIC and cannot be linked into a .so.  Use shared libs instead;
# the AppImage bundles them or the user's system provides them.

# --- platform-specific screen capture ---
if(APPLE)
    set(CAPTURE_SOURCES src/video/capture/screen_capture_mac.cpp)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_mac.mm)
    set_source_files_properties(src/audio/capture/system_audio_capture_mac.mm PROPERTIES
        LANGUAGE CXX
        COMPILE_FLAGS "-x objective-c++"
    )
elseif(WIN32)
    set(CAPTURE_SOURCES  src/video/capture/screen_capture_win.cpp)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_win.cpp)
elseif(UNIX)
    set(CAPTURE_SOURCES src/video/capture/screen_capture_linux.cpp)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_linux.cpp)
endif()

# --- driscord_core executable ---
add_library(driscord_core STATIC
    src/driscord_core.cpp
    src/video/video.cpp
    src/video/screen.cpp
    src/video/screen_session.cpp
    src/audio/audio.cpp
    src/audio/audio_mixer.cpp
    src/transport.cpp
    src/audio_transport.cpp
    src/video_transport.cpp
    src/video/video_codec.cpp
    src/audio/opus_codec.cpp
    src/utils/enum_strings.cpp
    src/utils/ma_device.cpp
    ${CAPTURE_SOURCES}
    ${SYSTEM_AUDIO_SOURCES}
)

target_include_directories(driscord_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/video
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

target_link_libraries(driscord_core PRIVATE
    Opus::opus
    miniaudio_lib
    PkgConfig::FFMPEG
)
# PUBLIC: these headers are included by driscord_core's public .hpp files,
# so consumers need their include paths too.
#   config.hpp       → fmt/compile.h, fmt/format.h
#   transport.hpp    → rtc/rtc.hpp, nlohmann/json.hpp
target_link_libraries(driscord_core PUBLIC
    datachannel-static
    fmt::fmt
    nlohmann_json::nlohmann_json
)

if(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    target_link_libraries(driscord_core PRIVATE
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(driscord_core PRIVATE ${SCREENCAPTUREKIT_FRAMEWORK})
    endif()
elseif(WIN32)
    target_link_libraries(driscord_core PRIVATE
        d3d11 dxgi gdi32 mfuuid mfplat mf strmiids
        ole32 wmcodecdspuuid ksuser mmdevapi
        # Required when FFmpeg is statically linked (pulls in Windows crypto/net)
        ws2_32 bcrypt secur32
    )
elseif(UNIX)
    find_package(X11 REQUIRED)
    if(NOT X11_Xrandr_FOUND)
        message(FATAL_ERROR "Xrandr not found. Install libxrandr-dev.")
    endif()
    target_link_libraries(driscord_core PRIVATE X11::X11 X11::Xrandr)
    find_library(PULSE_SIMPLE pulse-simple)
    find_library(PULSE pulse)
    if(PULSE_SIMPLE AND PULSE)
        target_link_libraries(driscord_core PRIVATE ${PULSE_SIMPLE} ${PULSE})
    else()
        message(WARNING "PulseAudio not found. System audio capture will be unavailable on Linux.")
    endif()
endif()

if(MSVC)
    target_compile_options(driscord_core PRIVATE /W4)
    target_compile_definitions(driscord_core PRIVATE
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
else()
    target_compile_options(driscord_core PRIVATE -Wall -Wextra -Wpedantic)
endif()