include(FetchContent)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googlebenchmark)

set(BENCH_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/core/src/
    ${CMAKE_SOURCE_DIR}/core/src/utils
    ${CMAKE_SOURCE_DIR}/core/tests/support
)

set(BENCH_CORE_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/core/src/
    ${CMAKE_SOURCE_DIR}/core/src/utils
)

if(MSVC)
    set(BENCH_WARNING_FLAGS /W4)
else()
    set(BENCH_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

# bench_playout — reorder buffer, delay estimation, playout clock, WSOLA
add_executable(bench_playout
    bench_playout.cpp
    ${CMAKE_SOURCE_DIR}/core/src/sync/media_clock.cpp
    ${CMAKE_SOURCE_DIR}/core/src/audio/wsola.cpp)
target_include_directories(bench_playout PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(bench_playout PRIVATE benchmark::benchmark_main)

# bench_protocol
add_executable(bench_protocol bench_protocol.cpp)
target_include_directories(bench_protocol PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(bench_protocol PRIVATE benchmark::benchmark_main)

# bench_video_codec (needs driscord_core for VideoEncoder/VideoDecoder)
add_executable(bench_video_codec bench_video_codec.cpp)
target_include_directories(bench_video_codec PRIVATE ${BENCH_CORE_INCLUDE_DIRS})
target_link_libraries(bench_video_codec PRIVATE benchmark::benchmark_main driscord_core)

# bench_net_conditions (header-only NetworkConditioner, no driscord_core needed)
add_executable(bench_net_conditions bench_net_conditions.cpp)
target_include_directories(bench_net_conditions PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(bench_net_conditions PRIVATE benchmark::benchmark_main)

foreach(bench bench_playout bench_protocol bench_video_codec bench_net_conditions)
    target_compile_options(${bench} PRIVATE ${BENCH_WARNING_FLAGS})
endforeach()
