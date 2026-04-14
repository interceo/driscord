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
)

set(BENCH_CORE_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/core/src/
    ${CMAKE_SOURCE_DIR}/core/src/utils
)

# bench_jitter
add_executable(bench_jitter bench_jitter.cpp)
target_include_directories(bench_jitter PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(bench_jitter PRIVATE benchmark::benchmark_main)

# bench_protocol
add_executable(bench_protocol bench_protocol.cpp)
target_include_directories(bench_protocol PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(bench_protocol PRIVATE benchmark::benchmark_main)

# bench_video_codec (needs driscord_core for VideoEncoder/VideoDecoder)
add_executable(bench_video_codec bench_video_codec.cpp)
target_include_directories(bench_video_codec PRIVATE ${BENCH_CORE_INCLUDE_DIRS})
target_link_libraries(bench_video_codec PRIVATE benchmark::benchmark_main driscord_core)
