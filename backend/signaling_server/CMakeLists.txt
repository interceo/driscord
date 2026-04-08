cmake_minimum_required(VERSION 3.20)

project(driscord_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(Boost 1.89 REQUIRED)

add_executable(driscord_server
    src/main.cpp
    src/ws_server.cpp
)

target_include_directories(driscord_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/core/src/utils
)

target_link_libraries(driscord_server PRIVATE
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    nlohmann_json::nlohmann_json
    fmt::fmt
)

if(MSVC)
    target_compile_options(driscord_server PRIVATE /W4)
    target_compile_definitions(driscord_server PRIVATE
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_options(driscord_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
