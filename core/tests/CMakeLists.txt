include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(googletest)

# Shared include directories:
#   - core/src/        — headers under test
#   - core/src/utils   — header-only utilities (jitter, slot_ring, ...)
#   - core/tests/support — test helpers (wait, fixtures, harness)
set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/core/src/
    ${CMAKE_SOURCE_DIR}/core/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)

# --- Helper to declare a unit test (header-only, no driscord_core) ---
function(add_unit_test name)
    add_executable(${name} unit/${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Helper for tests that need driscord_core (FFmpeg-backed codecs, etc.) ---
function(add_core_test name)
    add_executable(${name} unit/${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main driscord_core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_slot_ring)
add_unit_test(test_jitter)
add_unit_test(test_byte_utils)
add_core_test(test_protocol)
add_unit_test(test_spinlock)
add_unit_test(test_vector_view)
add_unit_test(test_chunk_assembler)
add_unit_test(test_expected)
add_core_test(test_video_codec)

# --- Integration tests (need driscord_core + driscord_signaling) ---
if(BUILD_SERVER)
    add_executable(test_datachannel_transport
        integration/test_datachannel_transport.cpp
    )
    target_include_directories(test_datachannel_transport PRIVATE
        ${TEST_INCLUDE_DIRS}
    )
    target_link_libraries(test_datachannel_transport PRIVATE
        GTest::gtest_main
        driscord_core
        driscord_signaling
    )
    add_test(NAME test_datachannel_transport
        COMMAND test_datachannel_transport)
    set_tests_properties(test_datachannel_transport PROPERTIES TIMEOUT 120)
endif()
