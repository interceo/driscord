cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(driscord VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

add_subdirectory(server)
add_subdirectory(client)
