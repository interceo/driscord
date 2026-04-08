include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest googlebenchmark)

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/client/src/
    ${CMAKE_SOURCE_DIR}/common
)

# test_slot_ring
add_executable(test_slot_ring test_slot_ring.cpp)
target_include_directories(test_slot_ring PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_slot_ring PRIVATE GTest::gtest_main)
add_test(NAME test_slot_ring COMMAND test_slot_ring)

# test_jitter
add_executable(test_jitter test_jitter.cpp)
target_include_directories(test_jitter PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_jitter PRIVATE GTest::gtest_main)
add_test(NAME test_jitter COMMAND test_jitter)

# test_byte_utils
add_executable(test_byte_utils test_byte_utils.cpp)
target_include_directories(test_byte_utils PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_byte_utils PRIVATE GTest::gtest_main)
add_test(NAME test_byte_utils COMMAND test_byte_utils)

# test_protocol
add_executable(test_protocol test_protocol.cpp)
target_include_directories(test_protocol PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_protocol PRIVATE GTest::gtest_main)
add_test(NAME test_protocol COMMAND test_protocol)

# test_spinlock
add_executable(test_spinlock test_spinlock.cpp)
target_include_directories(test_spinlock PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_spinlock PRIVATE GTest::gtest_main)
add_test(NAME test_spinlock COMMAND test_spinlock)

# test_vector_view
add_executable(test_vector_view test_vector_view.cpp)
target_include_directories(test_vector_view PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_vector_view PRIVATE GTest::gtest_main)
add_test(NAME test_vector_view COMMAND test_vector_view)

# test_chunk_assembler
add_executable(test_chunk_assembler test_chunk_assembler.cpp)
target_include_directories(test_chunk_assembler PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_chunk_assembler PRIVATE GTest::gtest_main)
add_test(NAME test_chunk_assembler COMMAND test_chunk_assembler)

# bench_jitter
add_executable(bench_jitter bench_jitter.cpp)
target_include_directories(bench_jitter PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(bench_jitter PRIVATE benchmark::benchmark_main)

# bench_protocol
add_executable(bench_protocol bench_protocol.cpp)
target_include_directories(bench_protocol PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(bench_protocol PRIVATE benchmark::benchmark_main)
