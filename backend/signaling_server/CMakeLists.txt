cmake_minimum_required(VERSION 3.20)

project(driscord_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(driscord_signaling STATIC
    src/media_connections.cpp
    src/rtp_slot_rewriter.cpp
    src/screen_router.cpp
    src/sfu_media_utils.cpp
    src/voice_router.cpp
    src/ws_server.cpp
)

target_include_directories(driscord_signaling PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/core/src/utils
    ${CMAKE_SOURCE_DIR}/core/src
)

target_link_libraries(driscord_signaling PUBLIC
    driscord_protocol
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    nlohmann_json::nlohmann_json
    fmt::fmt
    ${DRISCORD_LIBDATACHANNEL_TARGET}
)

if(MSVC)
    target_compile_options(driscord_signaling PRIVATE /W4)
    target_compile_definitions(driscord_signaling PUBLIC
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_options(driscord_signaling PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(driscord_server
    src/main.cpp
)

target_link_libraries(driscord_server PRIVATE
    driscord_signaling
)
