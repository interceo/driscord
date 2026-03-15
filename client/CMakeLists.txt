cmake_minimum_required(VERSION 3.20)

project(driscord_client LANGUAGES C CXX)

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

# --- Dear ImGui ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
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

# --- nlohmann/json ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# --- miniaudio (header-only) ---
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw imgui libdatachannel opus json miniaudio)

# Build Dear ImGui as a library (it has no CMakeLists.txt)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

if(APPLE)
    target_compile_definitions(imgui_lib PUBLIC GL_SILENCE_DEPRECATION)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    target_link_libraries(imgui_lib PUBLIC
        ${OPENGL_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
    )
elseif(WIN32)
    target_link_libraries(imgui_lib PUBLIC opengl32)
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui_lib PUBLIC OpenGL::GL)
endif()

# miniaudio interface library
add_library(miniaudio_lib INTERFACE)
target_include_directories(miniaudio_lib INTERFACE ${miniaudio_SOURCE_DIR})

# --- FFmpeg (system dependency) ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
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

# --- platform-specific screen capture ---
if(APPLE)
    set(CAPTURE_SOURCES src/video/capture/screen_capture_mac.cpp)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_mac.mm)
    set_source_files_properties(src/audio/capture/system_audio_capture_mac.mm PROPERTIES
        LANGUAGE CXX
        COMPILE_FLAGS "-x objective-c++"
    )
elseif(WIN32)
    set(CAPTURE_SOURCES src/video/capture/screen_capture_win.cpp)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_win.cpp)
elseif(UNIX)
    set(CAPTURE_SOURCES src/video/capture/screen_capture_linux.cpp)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_linux.cpp)
endif()

# --- driscord_client executable ---
add_executable(driscord_client
    src/main.cpp
    src/app.cpp
    src/video/video_sender.cpp
    src/video/video_receiver.cpp
    src/audio/audio_sender.cpp
    src/audio/audio_receiver.cpp
    src/audio/audio_mixer.cpp
    src/voice_transport.cpp
    src/ui.cpp
    src/config.cpp
    src/utils/video_codec.cpp
    src/utils/opus_codec.cpp
    ${CAPTURE_SOURCES}
    ${SYSTEM_AUDIO_SOURCES}
    src/video_renderer.cpp
)

target_include_directories(driscord_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/video
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(driscord_client PRIVATE
    imgui_lib
    datachannel-static
    Opus::opus
    nlohmann_json::nlohmann_json
    miniaudio_lib
    PkgConfig::FFMPEG
    fmt::fmt
)

if(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    target_link_libraries(driscord_client PRIVATE
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(driscord_client PRIVATE ${SCREENCAPTUREKIT_FRAMEWORK})
    endif()
elseif(WIN32)
    target_link_libraries(driscord_client PRIVATE
        d3d11 dxgi gdi32 mfuuid mfplat mf strmiids
        ole32 wmcodecdspuuid ksuser mmdevapi
    )
elseif(UNIX)
    find_package(X11 REQUIRED)
    if(NOT X11_Xrandr_FOUND)
        message(FATAL_ERROR "Xrandr not found. Install libxrandr-dev.")
    endif()
    target_link_libraries(driscord_client PRIVATE X11::X11 X11::Xrandr)
    find_library(PULSE_SIMPLE pulse-simple)
    find_library(PULSE pulse)
    if(PULSE_SIMPLE AND PULSE)
        target_link_libraries(driscord_client PRIVATE ${PULSE_SIMPLE} ${PULSE})
    else()
        message(WARNING "PulseAudio not found. System audio capture will be unavailable on Linux.")
    endif()
endif()

if(MSVC)
    target_compile_options(driscord_client PRIVATE /W4)
    target_compile_definitions(driscord_client PRIVATE
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
else()
    target_compile_options(driscord_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
