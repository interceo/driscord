cmake_minimum_required(VERSION 3.20)

project(driscord_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Required so that driscord_core's objects can be linked into driscord_core_jni (.so)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

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

FetchContent_MakeAvailable(libdatachannel opus miniaudio)

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

# --- Vulkan (optional) ---
find_package(Vulkan QUIET)
set(VULKAN_SOURCES "")
if(Vulkan_FOUND)
    message(STATUS "Vulkan found: ${Vulkan_LIBRARY}")

    # Compile SPIR-V shaders and embed as C headers
    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT})

    set(SHADER_HEADERS "")
    foreach(SHADER quad_vert:quad.vert quad_frag:quad.frag)
        string(REPLACE ":" ";" PARTS ${SHADER})
        list(GET PARTS 0 NAME)
        list(GET PARTS 1 FILE)

        set(SPV_FILE ${SHADER_OUT}/${NAME}.spv)
        set(HDR_FILE ${SHADER_OUT}/${NAME}.h)

        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND Vulkan::glslc ${SHADER_DIR}/${FILE} -o ${SPV_FILE}
            DEPENDS ${SHADER_DIR}/${FILE}
            COMMENT "Compiling ${FILE} -> ${NAME}.spv"
        )
        add_custom_command(
            OUTPUT ${HDR_FILE}
            COMMAND ${CMAKE_COMMAND} -P ${SHADER_DIR}/spv_to_header.cmake
                    ${SPV_FILE} ${HDR_FILE} ${NAME}_spv
            DEPENDS ${SPV_FILE} ${SHADER_DIR}/spv_to_header.cmake
            COMMENT "Embedding ${NAME}.spv as C header"
        )
        list(APPEND SHADER_HEADERS ${HDR_FILE})
    endforeach()

    add_custom_target(driscord_shaders DEPENDS ${SHADER_HEADERS})

    set(VULKAN_SOURCES
        src/video/vulkan_renderer.cpp
        src/video/vulkan_texture.cpp
    )
else()
    message(STATUS "Vulkan not found — video will use Skia fallback")
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
    ${VULKAN_SOURCES}
)

set_target_properties(driscord_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(driscord_core PRIVATE
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

if(Vulkan_FOUND)
    add_dependencies(driscord_core driscord_shaders)
    target_include_directories(driscord_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    target_link_libraries(driscord_core PRIVATE Vulkan::Vulkan)
    target_compile_definitions(driscord_core PRIVATE DRISCORD_HAS_VULKAN=1)
endif()
# PUBLIC: these headers are included by driscord_core's public .hpp files,
# so consumers (driscord_core_jni) need their include paths too.
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

# ---------------------------------------------------------------------------
# driscord_core_jni — shared library loaded by the Kotlin/Compose Desktop client
# ---------------------------------------------------------------------------
find_package(JNI QUIET)
if(JNI_FOUND)
    add_library(driscord_core_jni SHARED
        jni/driscord_state.cpp
        jni/driscord_jni_transport.cpp
        jni/driscord_jni_audio.cpp
        jni/driscord_jni_video.cpp
        jni/driscord_jni_capture.cpp
        jni/driscord_jni_screen.cpp
        jni/driscord_jni_vulkan.cpp
    )

    set_target_properties(driscord_core_jni PROPERTIES OUTPUT_NAME "core")

    target_include_directories(driscord_core_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/jni
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/src/video
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${JNI_INCLUDE_DIRS}
    )

    target_link_libraries(driscord_core_jni PRIVATE driscord_core ${JNI_LIBRARIES})

    if(Vulkan_FOUND)
        target_link_libraries(driscord_core_jni PRIVATE Vulkan::Vulkan)
        target_compile_definitions(driscord_core_jni PRIVATE DRISCORD_HAS_VULKAN=1)
    endif()

    # Transitive deps driscord_core was built with
    target_link_libraries(driscord_core_jni PRIVATE
        datachannel-static
        Opus::opus
        nlohmann_json::nlohmann_json
        miniaudio_lib
        PkgConfig::FFMPEG
        fmt::fmt
    )

    if(APPLE)
        target_link_libraries(driscord_core_jni PRIVATE
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${COREAUDIO_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
            ${COREGRAPHICS_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
        )
        if(SCREENCAPTUREKIT_FRAMEWORK)
            target_link_libraries(driscord_core_jni PRIVATE ${SCREENCAPTUREKIT_FRAMEWORK})
        endif()
    elseif(WIN32)
        target_link_libraries(driscord_core_jni PRIVATE
            d3d11 dxgi gdi32 mfuuid mfplat mf strmiids
            ole32 wmcodecdspuuid ksuser mmdevapi
        )
    elseif(UNIX)
        target_link_libraries(driscord_core_jni PRIVATE X11::X11 X11::Xrandr)
        if(PULSE_SIMPLE AND PULSE)
            target_link_libraries(driscord_core_jni PRIVATE ${PULSE_SIMPLE} ${PULSE})
        endif()
    endif()

    if(MSVC)
        target_compile_options(driscord_core_jni PRIVATE /W4)
        target_compile_definitions(driscord_core_jni PRIVATE
            _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(driscord_core_jni PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    message(STATUS "driscord_core_jni target configured (JNI found at ${JNI_INCLUDE_DIRS})")
else()
    message(STATUS "JNI not found — skipping driscord_core_jni shared library")
endif()