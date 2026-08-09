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
#   - core/src/utils   — header-only utilities (reorder_buffer, expected, ...)
#   - core/tests/support — test helpers (wait, fixtures, harness)
set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/core/src/
    ${CMAKE_SOURCE_DIR}/core/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)

if(MSVC)
    set(TEST_WARNING_FLAGS /W4)
else()
    set(TEST_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

# --- Helper to declare a unit test (no driscord_core) ---
# Extra arguments are additional source files, for components that are small
# and self-contained enough to compile straight into the test.
function(add_unit_test name)
    add_executable(${name} unit/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main Boost::boost)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Helper for tests that need driscord_core (FFmpeg-backed codecs, etc.) ---
function(add_core_test name)
    add_executable(${name} unit/${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main driscord_core Boost::boost)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_reorder_buffer)
add_unit_test(test_delay_estimator)
add_unit_test(test_media_clock ${CMAKE_SOURCE_DIR}/core/src/sync/media_clock.cpp)
add_unit_test(test_wsola ${CMAKE_SOURCE_DIR}/core/src/audio/wsola.cpp)
add_core_test(test_protocol)
add_unit_test(test_spinlock)
add_unit_test(test_expected)
add_unit_test(test_bounded_queue)
add_unit_test(test_match)

# --- Connection lifecycle state machines ---
# Both tables are deliberately free of libdatachannel/Beast, so they run as
# plain unit tests: no socket, no PeerConnection, no timing.
add_unit_test(test_transport_fsm)
target_link_libraries(test_transport_fsm PRIVATE sml::sml)

add_unit_test(test_session_fsm)
target_link_libraries(test_session_fsm PRIVATE sml::sml)
target_include_directories(test_session_fsm PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/signaling_server/src)
add_core_test(test_video_codec)
add_core_test(test_video_receiver)
add_core_test(test_audio_receiver)

# --- Media-quality scenarios ---
# Deterministic: simulated clock, seeded network model, no sockets. Not gated on
# BUILD_SERVER and not RUN_SERIAL, unlike the integration tests.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(AVFILTER QUIET IMPORTED_TARGET libavfilter libavutil libswscale)
endif()

if(TARGET PkgConfig::AVFILTER)
    function(add_quality_test name)
        add_executable(${name} quality/${name}.cpp)
        target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
        target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
        target_link_libraries(${name} PRIVATE
            GTest::gtest driscord_core Boost::boost PkgConfig::AVFILTER)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES TIMEOUT 300)
    endfunction()

    add_quality_test(test_media_quality)
    add_quality_test(test_playout_policies)
else()
    message(STATUS "libavfilter not found - media-quality tests disabled")
endif()

# --- Integration tests (need driscord_core + driscord_signaling) ---
if(BUILD_SERVER)
    # Helper to declare an integration test linked against both core and signaling.
    function(add_integration_test name)
        add_executable(${name} integration/${name}.cpp)
        target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
        target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
        target_link_libraries(${name} PRIVATE
            GTest::gtest_main driscord_core driscord_signaling)
        add_test(NAME ${name} COMMAND ${name})
        # These spin up real WebSocket servers + PeerConnections and assert on
        # timing (playout deadlines, A/V skew). Under `ctest -j` alongside the
        # CPU-heavy codec tests they flake, so pin them to run alone.
        set_tests_properties(${name} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endfunction()

    add_integration_test(test_sfu_transport)
    add_integration_test(test_room_isolation)
    add_integration_test(test_net_conditions)
    add_integration_test(test_av_sync)
    set_tests_properties(test_net_conditions PROPERTIES TIMEOUT 180)
endif()
