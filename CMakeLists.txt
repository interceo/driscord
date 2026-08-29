cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

include(cmake/Version.cmake)
project(driscord VERSION ${DRISCORD_VERSION_CORE} LANGUAGES C CXX)

configure_file(cmake/version.hpp.in
    "${CMAKE_BINARY_DIR}/generated/driscord/version.hpp" @ONLY)
message(STATUS "Driscord version: ${DRISCORD_VERSION_DISPLAY}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_CORE "Build the core library" ON)
option(BUILD_SERVER "Build the signaling server" ON)

option(DRISCORD_FUZZ "Build libFuzzer fuzz targets (requires Clang)" OFF)

set(DRISCORD_SANITIZE "" CACHE STRING
    "Comma-separated -fsanitize= list applied to the whole tree")

option(DRISCORD_COVERAGE
    "Instrument with -fprofile-instr-generate/-fcoverage-mapping" OFF)

if(DRISCORD_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DRISCORD_FUZZ requires Clang (libFuzzer)")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined
        -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
elseif(DRISCORD_SANITIZE)
    add_compile_options(-fsanitize=${DRISCORD_SANITIZE}
        -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${DRISCORD_SANITIZE})
endif()

if(DRISCORD_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DRISCORD_COVERAGE requires Clang (llvm-cov)")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

option(DRISCORD_USE_GOOGLE_WEBRTC
    "Build the Google WebRTC client media backend" ON)
if(BUILD_CORE AND NOT DRISCORD_USE_GOOGLE_WEBRTC)
    message(FATAL_ERROR
        "The legacy media backend was removed; BUILD_CORE requires "
        "DRISCORD_USE_GOOGLE_WEBRTC=ON.")
endif()
if(BUILD_CORE)
    include(cmake/GoogleWebRTC.cmake)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

include(FetchContent)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        12.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fmt)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

option(DRISCORD_ENABLE_RTP_MEDIA
    "Build libdatachannel RTP/RTCP media support for the SFU" ON)
if(BUILD_SERVER AND DRISCORD_ENABLE_RTP_MEDIA)
    set(NO_MEDIA OFF CACHE BOOL "" FORCE)
else()
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
endif()
if(BUILD_CORE AND WIN32)
    set(USE_GNUTLS OFF CACHE BOOL "Use GnuTLS instead of OpenSSL" FORCE)
    set(USE_MBEDTLS ON CACHE BOOL "Use Mbed TLS instead of OpenSSL" FORCE)
    unset(OPENSSL CACHE)
    unset(NSS CACHE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_USER_CONFIG_FILE
        "${CMAKE_SOURCE_DIR}/cmake/mbedtls_user_config.h" CACHE FILEPATH "" FORCE)
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.6.7
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(mbedtls)
    if(NOT TARGET MbedTLS::MbedTLS)
        add_library(MbedTLS::MbedTLS UNKNOWN IMPORTED GLOBAL)
        set_target_properties(MbedTLS::MbedTLS PROPERTIES
            IMPORTED_LOCATION "${mbedtls_BINARY_DIR}/library/mbedtls.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${mbedtls_SOURCE_DIR}/include"
            INTERFACE_COMPILE_DEFINITIONS
                "MBEDTLS_USER_CONFIG_FILE=\"${CMAKE_SOURCE_DIR}/cmake/mbedtls_user_config.h\""
            INTERFACE_LINK_LIBRARIES mbedtls)
    endif()
    set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    foreach(_driscord_mbed_component Crypto X509)
        string(TOLOWER "mbed${_driscord_mbed_component}" _driscord_mbed_lib)
        if(NOT TARGET MbedTLS::Mbed${_driscord_mbed_component})
            add_library(MbedTLS::Mbed${_driscord_mbed_component}
                UNKNOWN IMPORTED GLOBAL)
            set_target_properties(MbedTLS::Mbed${_driscord_mbed_component}
                PROPERTIES
                IMPORTED_LOCATION
                    "${mbedtls_BINARY_DIR}/library/${_driscord_mbed_lib}.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${mbedtls_SOURCE_DIR}/include")
        endif()
    endforeach()
    unset(_driscord_mbed_component)
    unset(_driscord_mbed_lib)
    set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include"
        CACHE PATH "" FORCE)
    set(MbedTLS_LIBRARY "${mbedtls_BINARY_DIR}/library/mbedtls.lib"
        CACHE FILEPATH "" FORCE)
    set(MbedX509_LIBRARY "${mbedtls_BINARY_DIR}/library/mbedx509.lib"
        CACHE FILEPATH "" FORCE)
    set(MbedCrypto_LIBRARY "${mbedtls_BINARY_DIR}/library/mbedcrypto.lib"
        CACHE FILEPATH "" FORCE)
elseif(BUILD_CORE)
    set(USE_GNUTLS ON CACHE BOOL "Use GnuTLS instead of OpenSSL" FORCE)
    set(USE_MBEDTLS OFF CACHE BOOL "Use Mbed TLS instead of OpenSSL" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL
        "Disable OpenSSL in bundled libSRTP for BoringSSL coexistence" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL
        "Disable Mbed TLS crypto in bundled libSRTP" FORCE)
    set(ENABLE_NSS ON CACHE BOOL
        "Use NSS crypto in bundled libSRTP" FORCE)
    unset(OPENSSL CACHE)
    unset(MBEDTLS CACHE)
else()
    set(USE_GNUTLS OFF CACHE BOOL "Use GnuTLS instead of OpenSSL" FORCE)
    set(ENABLE_OPENSSL ON CACHE BOOL
        "Use OpenSSL crypto in bundled libSRTP" FORCE)
    set(ENABLE_NSS OFF CACHE BOOL
        "Disable NSS crypto in bundled libSRTP" FORCE)
    unset(NSS CACHE)
    unset(MBEDTLS CACHE)
endif()
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.24.5
    GIT_SHALLOW    TRUE
    PATCH_COMMAND
        "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_SOURCE_DIR}/third_party/patches/libdatachannel-gnutls-partial-write.patch"
        -P "${CMAKE_SOURCE_DIR}/cmake/ApplyGitPatch.cmake"
)
if(BUILD_CORE AND NOT WIN32)
    set(_driscord_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS ON)
endif()
FetchContent_MakeAvailable(libdatachannel)
if(BUILD_CORE AND NOT WIN32)
    set(BUILD_SHARED_LIBS "${_driscord_saved_build_shared_libs}")
    unset(_driscord_saved_build_shared_libs)
    set(DRISCORD_LIBDATACHANNEL_TARGET datachannel)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(datachannel PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
else()
    set(DRISCORD_LIBDATACHANNEL_TARGET datachannel-static)
endif()

if(TARGET datachannel AND NOT DRISCORD_LIBDATACHANNEL_TARGET STREQUAL "datachannel")
    set_target_properties(datachannel PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

if(WIN32 AND TARGET datachannel-static AND TARGET mbedtls)
    add_dependencies(datachannel-static mbedtls)
endif()

find_package(Boost 1.89 REQUIRED)

add_library(driscord_protocol STATIC
    core/src/utils/signaling_protocol.cpp
)
target_include_directories(driscord_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/core/src
    ${CMAKE_SOURCE_DIR}/core/src/utils
)
target_link_libraries(driscord_protocol PUBLIC
    nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(driscord_protocol PRIVATE /W4)
else()
    target_compile_options(driscord_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVER)
    add_subdirectory(backend/signaling_server)
endif()

if(BUILD_CORE)
    add_subdirectory(core)
endif()

option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

option(BUILD_QT_CLIENT "Build the Qt/QML client" OFF)
if(BUILD_QT_CLIENT)
    set(DRISCORD_CLIENT_SIGNALING_URL "ws://localhost:9001" CACHE STRING
        "WebSocket base URL compiled into the Driscord client")
    set(DRISCORD_CLIENT_API_URL "http://localhost:9002" CACHE STRING
        "HTTP base URL compiled into the Driscord client")
    set(DRISCORD_CLIENT_UPDATE_URL
        "https://updates.homelab.ceooptimizator.tech/driscord" CACHE STRING
        "Base URL of the static update channel tree; empty disables the updater")
    set(DRISCORD_CLIENT_UPDATE_CHANNEL "stable" CACHE STRING
        "Update channel compiled into the Driscord client")

    if(NOT DRISCORD_CLIENT_SIGNALING_URL MATCHES "^wss?://[^/]+")
        message(FATAL_ERROR
            "DRISCORD_CLIENT_SIGNALING_URL must be an absolute ws:// or wss:// URL")
    endif()
    if(NOT DRISCORD_CLIENT_API_URL MATCHES "^https?://[^/]+")
        message(FATAL_ERROR
            "DRISCORD_CLIENT_API_URL must be an absolute http:// or https:// URL")
    endif()
    if(NOT DRISCORD_CLIENT_UPDATE_URL STREQUAL ""
        AND NOT DRISCORD_CLIENT_UPDATE_URL MATCHES "^https?://[^/]+")
        message(FATAL_ERROR
            "DRISCORD_CLIENT_UPDATE_URL must be empty or an absolute "
            "http:// or https:// URL")
    endif()
    if(NOT DRISCORD_CLIENT_UPDATE_CHANNEL MATCHES "^[a-z0-9-]+$")
        message(FATAL_ERROR
            "DRISCORD_CLIENT_UPDATE_CHANNEL must match [a-z0-9-]+")
    endif()
    foreach(_driscord_client_url IN ITEMS
            DRISCORD_CLIENT_SIGNALING_URL DRISCORD_CLIENT_API_URL
            DRISCORD_CLIENT_UPDATE_URL)
        if(${_driscord_client_url} MATCHES "[\\\"]")
            message(FATAL_ERROR
                "${_driscord_client_url} contains a character that is invalid in a URL")
        endif()
    endforeach()
    unset(_driscord_client_url)

    string(REGEX REPLACE "/+$" "" DRISCORD_CLIENT_SIGNALING_URL_VALUE
        "${DRISCORD_CLIENT_SIGNALING_URL}")
    string(REGEX REPLACE "/+$" "" DRISCORD_CLIENT_API_URL_VALUE
        "${DRISCORD_CLIENT_API_URL}")
    string(REGEX REPLACE "/+$" "" DRISCORD_CLIENT_UPDATE_URL_VALUE
        "${DRISCORD_CLIENT_UPDATE_URL}")
    configure_file(cmake/client_build_config.hpp.in
        "${CMAKE_BINARY_DIR}/generated/driscord/client_build_config.hpp" @ONLY)
    message(STATUS
        "Driscord client endpoints: ${DRISCORD_CLIENT_SIGNALING_URL_VALUE}, "
        "${DRISCORD_CLIENT_API_URL_VALUE}; update channel: "
        "${DRISCORD_CLIENT_UPDATE_URL_VALUE}/${DRISCORD_CLIENT_UPDATE_CHANNEL}")

    add_subdirectory(client-qt)
endif()

option(DRISCORD_ENABLE_PACKAGING "Create a portable client package" OFF)
if(DRISCORD_ENABLE_PACKAGING)
    if(NOT BUILD_QT_CLIENT)
        message(FATAL_ERROR
            "DRISCORD_ENABLE_PACKAGING requires BUILD_QT_CLIENT=ON")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _driscord_package_processor)
    if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$"
        OR NOT _driscord_package_processor MATCHES "^(x86_64|amd64)$")
        message(FATAL_ERROR
            "The release package supports Linux and Windows x86_64 only")
    endif()
    unset(_driscord_package_processor)

    set(_driscord_default_package_dir "${CMAKE_BINARY_DIR}/packages")
    if(DEFINED ENV{DRISCORD_RELEASE_STAGING}
        AND NOT "$ENV{DRISCORD_RELEASE_STAGING}" STREQUAL "")
        set(_driscord_default_package_dir "$ENV{DRISCORD_RELEASE_STAGING}")
    endif()
    set(DRISCORD_RELEASE_STAGING "${_driscord_default_package_dir}"
        CACHE PATH "Directory for generated release artifacts")

    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
        set(_driscord_package_basename
            "driscord-client-${DRISCORD_VERSION_DISPLAY}-windows-x86_64")
        set(_driscord_verify_script
            "${CMAKE_SOURCE_DIR}/cmake/VerifyPackageWindows.cmake")
    else()
        set(CPACK_GENERATOR "External")
        set(_driscord_package_basename
            "driscord-client-${DRISCORD_VERSION_DISPLAY}-linux-x86_64")
        set(_driscord_verify_script
            "${CMAKE_SOURCE_DIR}/cmake/VerifyPackage.cmake")
        set(CPACK_EXTERNAL_ENABLE_STAGING ON)
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT
            "${CMAKE_SOURCE_DIR}/cmake/PackageAppImage.cmake")
        set(_driscord_appimage_runtime_default
            "${CMAKE_SOURCE_DIR}/.cache/appimage-runtime-x86_64")
        if(DEFINED ENV{DRISCORD_APPIMAGE_RUNTIME}
                AND NOT "$ENV{DRISCORD_APPIMAGE_RUNTIME}" STREQUAL "")
            set(_driscord_appimage_runtime_default
                "$ENV{DRISCORD_APPIMAGE_RUNTIME}")
        endif()
        set(DRISCORD_APPIMAGE_RUNTIME
            "${_driscord_appimage_runtime_default}" CACHE FILEPATH
            "Static type-2 AppImage runtime prepended to the squashfs payload")
        unset(_driscord_appimage_runtime_default)
        if(NOT EXISTS "${DRISCORD_APPIMAGE_RUNTIME}")
            message(FATAL_ERROR
                "DRISCORD_APPIMAGE_RUNTIME is missing: "
                "${DRISCORD_APPIMAGE_RUNTIME}")
        endif()
        set(CPACK_DRISCORD_APPIMAGE_RUNTIME "${DRISCORD_APPIMAGE_RUNTIME}")
    endif()
    set(CPACK_PACKAGE_NAME "driscord-client")
    set(CPACK_PACKAGE_VENDOR "Driscord")
    set(CPACK_PACKAGE_VERSION "${DRISCORD_VERSION_DISPLAY}")
    set(CPACK_PACKAGE_FILE_NAME "${_driscord_package_basename}")
    set(CPACK_PACKAGE_DIRECTORY "${DRISCORD_RELEASE_STAGING}")
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    set(CPACK_MONOLITHIC_INSTALL OFF)
    set(CPACK_COMPONENTS_ALL Runtime)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    set(CPACK_ARCHIVE_FILE_NAME "${_driscord_package_basename}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PRE_BUILD_SCRIPTS "${_driscord_verify_script}")
    include(CPack)
    unset(_driscord_verify_script)
    unset(_driscord_package_basename)
    unset(_driscord_default_package_dir)
endif()
