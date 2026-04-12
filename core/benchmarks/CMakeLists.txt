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

# jitter_bench
add_executable(jitter_bench jitter_bench.cpp)
target_include_directories(jitter_bench PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(jitter_bench PRIVATE benchmark::benchmark_main)

# protocol_bench
add_executable(protocol_bench protocol_bench.cpp)
target_include_directories(protocol_bench PRIVATE ${BENCH_INCLUDE_DIRS})
target_link_libraries(protocol_bench PRIVATE benchmark::benchmark_main)

# video_codec_bench (needs driscord_core for VideoEncoder/VideoDecoder)
add_executable(video_codec_bench video_codec_bench.cpp)
target_include_directories(video_codec_bench PRIVATE ${BENCH_CORE_INCLUDE_DIRS})
target_link_libraries(video_codec_bench PRIVATE benchmark::benchmark_main driscord_core)
