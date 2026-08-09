cmake_minimum_required(VERSION 3.20)

project(driscord_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# libdatachannel is declared in the root CMakeLists.txt — it is shared with the
# signaling server, which links it to terminate client PeerConnections (SFU).

# --- miniaudio (header-only) ---
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(miniaudio)

# miniaudio interface library
add_library(miniaudio_lib INTERFACE)
target_include_directories(miniaudio_lib INTERFACE ${miniaudio_SOURCE_DIR})

# --- platform-specific system loopback capture ---
if(APPLE)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_mac.mm)
    set_source_files_properties(src/audio/capture/system_audio_capture_mac.mm PROPERTIES
        LANGUAGE CXX
        COMPILE_FLAGS "-x objective-c++"
    )
elseif(WIN32)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_win.cpp)
elseif(UNIX)
    set(SYSTEM_AUDIO_SOURCES src/audio/capture/system_audio_capture_linux.cpp)
endif()

# --- driscord_core executable ---
add_library(driscord_core STATIC
    src/driscord_core.cpp
    src/webrtc/google_webrtc_client.cpp
    src/webrtc/google_webrtc_pcm_playout.cpp
    src/webrtc/google_webrtc_screen_stats.cpp
    src/transport.cpp
    src/utils/ma_device.cpp
    ${SYSTEM_AUDIO_SOURCES}
)

# The backend remains an ABI/PIMPL boundary: no Google WebRTC type reaches the
# public DriscordCore or Qt headers.
add_library(driscord_webrtc_backend STATIC
    src/webrtc/google_webrtc_runtime.cpp
    src/webrtc/google_webrtc_peer_adapters.cpp
    src/webrtc/google_webrtc_screen_adapters.cpp
    src/webrtc/google_webrtc_screen_session.cpp
    src/webrtc/google_webrtc_voice_adapters.cpp
    src/webrtc/google_webrtc_voice_session.cpp
)
target_include_directories(driscord_webrtc_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(driscord_webrtc_backend PRIVATE
    driscord::google_webrtc
)
if(MSVC)
    target_compile_options(driscord_webrtc_backend PRIVATE /W4)
else()
    target_compile_options(driscord_webrtc_backend PRIVATE
        -Wall -Wextra -Wpedantic -fno-rtti)
endif()

target_include_directories(driscord_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

target_link_libraries(driscord_core PRIVATE
    miniaudio_lib
    driscord_webrtc_backend
)
# PUBLIC: these headers are included by driscord_core's public .hpp files,
# so consumers need their include paths too.
#   config.hpp       → fmt/compile.h, fmt/format.h
#   transport.hpp    → rtc/rtc.hpp, nlohmann/json.hpp
target_link_libraries(driscord_core PUBLIC
    driscord_protocol
    ${DRISCORD_LIBDATACHANNEL_TARGET}
    fmt::fmt
    nlohmann_json::nlohmann_json
)

if(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(driscord_core PRIVATE
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
elseif(WIN32)
    target_link_libraries(driscord_core PRIVATE
        ole32 ksuser mmdevapi
    )
elseif(UNIX)
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
