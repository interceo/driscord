cmake_minimum_required(VERSION 3.20)

project(driscord_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.78 REQUIRED COMPONENTS system)

include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

add_executable(driscord_server
    src/main.cpp
    src/ws_server.cpp
)

target_include_directories(driscord_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(driscord_server PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(driscord_server PRIVATE /W4)
else()
    target_compile_options(driscord_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
