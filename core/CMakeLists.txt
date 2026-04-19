cmake_minimum_required(VERSION 3.20)

project(driscord_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Required so that driscord_core's objects can be linked into driscord_core_jni (.so)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
        # Link against BtbN's shared (.dll.a) import libs. The FFmpeg DLLs
        # (avcodec-*.dll, avformat-*.dll, etc.) must be shipped next to
        # core.dll at runtime — build.sh copies them into the native staging
        # dir, Gradle embeds them into the JAR, NativeLoader extracts them.
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
# On Linux: search for .a files in the same dirs pkg-config reported.
# On Windows (cross-compile): already handled above with explicit .a paths.
# ---------------------------------------------------------------------------
if(NOT (CMAKE_CROSSCOMPILING AND WIN32) AND TARGET PkgConfig::FFMPEG)
    set(_ff_static "")
    foreach(_ff avcodec avformat avdevice swscale swresample avutil)
        find_library(_ff_a_${_ff}
            NAMES "lib${_ff}.a"
            HINTS ${FFMPEG_LIBRARY_DIRS}
            NO_DEFAULT_PATH
        )
        if(_ff_a_${_ff})
            list(APPEND _ff_static "${_ff_a_${_ff}}")
        endif()
        unset(_ff_a_${_ff} CACHE)
    endforeach()

    list(LENGTH _ff_static _ff_static_count)
    if(_ff_static_count EQUAL 6)
        set_target_properties(PkgConfig::FFMPEG PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_ff_static}")
        message(STATUS "FFmpeg: static .a found — baked into JNI library")
    else()
        message(STATUS "FFmpeg: static .a not available — using shared libs "
            "(AppImage must bundle libav*.so or target system needs FFmpeg installed)")
    endif()
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

# ---------------------------------------------------------------------------
# driscord_core_jni — shared library loaded by the Kotlin/Compose Desktop client
# ---------------------------------------------------------------------------
# When cross-compiling to Windows via MinGW, find_package(JNI) fails because
# the toolchain restricts header search to CMAKE_FIND_ROOT_PATH and the host
# Linux JDK isn't in there. Resolve JNI manually: use the host JDK's jni.h
# (platform-agnostic) together with our vendored win32/jni_md.h.
if(CMAKE_CROSSCOMPILING AND WIN32)
    set(_HOST_JDK "$ENV{JAVA_HOME}")
    if(NOT _HOST_JDK OR NOT EXISTS "${_HOST_JDK}/include/jni.h")
        # Fallback: derive from `which java` like build.sh does.
        find_program(_HOST_JAVA java)
        if(_HOST_JAVA)
            get_filename_component(_HOST_JAVA_REAL "${_HOST_JAVA}" REALPATH)
            get_filename_component(_HOST_JDK "${_HOST_JAVA_REAL}" DIRECTORY)
            get_filename_component(_HOST_JDK "${_HOST_JDK}" DIRECTORY)
        endif()
    endif()
    if(EXISTS "${_HOST_JDK}/include/jni.h")
        set(JNI_INCLUDE_DIRS
            "${_HOST_JDK}/include"
            "${CMAKE_SOURCE_DIR}/cmake/jni-win32"
        )
        # No JNI_LIBRARIES on Windows: JNI symbols resolve at load time
        # against the JVM that dlopen's the DLL.
        set(JNI_LIBRARIES "")
        set(JNI_FOUND TRUE)
        message(STATUS "JNI (Windows cross): host JDK ${_HOST_JDK} + vendored jni_md.h")
    else()
        message(WARNING "JNI (Windows cross): host JDK not found — core.dll will not be built")
    endif()
else()
    find_package(JNI QUIET)
endif()
if(JNI_FOUND)
    add_library(driscord_core_jni SHARED
        jni/driscord_state.cpp
        jni/driscord_jni_transport.cpp
        jni/driscord_jni_audio.cpp
        jni/driscord_jni_video.cpp
        jni/driscord_jni_capture.cpp
        jni/driscord_jni_screen.cpp
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
            ws2_32 bcrypt secur32
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