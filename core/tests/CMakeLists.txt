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
#   - core/src/utils   — header-only utilities (expected, bounded_queue, ...)
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

# --- Helper for tests that need the Google WebRTC client media stack ---
function(add_core_test name)
    add_executable(${name} unit/${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main driscord_core Boost::boost)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_expected)
add_unit_test(test_bounded_queue)
add_unit_test(test_match)

# The wire format is shared by the client and the server, so its test must not
# drag in either media stack.
add_executable(test_protocol unit/test_protocol.cpp)
target_include_directories(test_protocol PRIVATE ${TEST_INCLUDE_DIRS})
target_compile_options(test_protocol PRIVATE ${TEST_WARNING_FLAGS})
target_link_libraries(test_protocol PRIVATE
    GTest::gtest_main driscord_protocol Boost::boost)
add_test(NAME test_protocol COMMAND test_protocol)

# --- Server-side tests: no Google WebRTC, so a BUILD_CORE=OFF tree runs them --
if(BUILD_SERVER)
    function(add_signaling_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE
            ${TEST_INCLUDE_DIRS}
            ${CMAKE_SOURCE_DIR}/backend/signaling_server/src)
        target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
        target_link_libraries(${name} PRIVATE
            GTest::gtest_main driscord_signaling)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_signaling_test(test_rtp_slot_rewriter unit/test_rtp_slot_rewriter.cpp)
    add_signaling_test(test_sfu_media_utils unit/test_sfu_media_utils.cpp)

    # Real WebSocket handshakes against the real server, but no media: this is
    # the gate on session authorization.
    add_signaling_test(test_signaling_auth integration/test_signaling_auth.cpp)
    target_link_libraries(test_signaling_auth PRIVATE
        ${DRISCORD_LIBDATACHANNEL_TARGET})
    set_tests_properties(test_signaling_auth PROPERTIES TIMEOUT 60)
endif()

if(BUILD_CORE)
    add_core_test(test_google_webrtc_screen_stats)

    # libdatachannel's GnuTLS backend is patched to retry positive partial
    # writes. A 32 KiB text frame crosses the TLS record boundary and regresses
    # the exact failure that previously truncated SDP and produced WebSocket
    # close code 1007.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(test_libdatachannel_wss_client
        support/libdatachannel_wss_client.cpp)
    target_compile_options(test_libdatachannel_wss_client PRIVATE
        ${TEST_WARNING_FLAGS})
    target_link_libraries(test_libdatachannel_wss_client PRIVATE
        ${DRISCORD_LIBDATACHANNEL_TARGET})
    add_test(NAME test_libdatachannel_wss
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/support/test_libdatachannel_wss.py
            --client $<TARGET_FILE:test_libdatachannel_wss_client>
            --cert ${CMAKE_CURRENT_SOURCE_DIR}/support/wss-test-cert.pem
            --key ${CMAKE_CURRENT_SOURCE_DIR}/support/wss-test-key.pem)
    set_tests_properties(test_libdatachannel_wss PROPERTIES TIMEOUT 30)

    add_executable(test_google_webrtc_runtime
        unit/test_google_webrtc_runtime.cpp)
    target_include_directories(test_google_webrtc_runtime PRIVATE
        ${TEST_INCLUDE_DIRS})
    target_compile_options(test_google_webrtc_runtime PRIVATE
        ${TEST_WARNING_FLAGS})
    target_link_libraries(test_google_webrtc_runtime PRIVATE
        GTest::gtest_main driscord_webrtc_backend)
    add_test(NAME test_google_webrtc_runtime COMMAND test_google_webrtc_runtime)
    set_tests_properties(test_google_webrtc_runtime PROPERTIES TIMEOUT 30)
endif()

# --- Integration tests (need driscord_core + driscord_signaling) ---
if(BUILD_SERVER AND BUILD_CORE)
    # Integration tests use real WebSocket signaling and WebRTC/SRTP media.
    function(add_integration_test name)
        add_executable(${name} integration/${name}.cpp)
        target_include_directories(${name} PRIVATE
            ${TEST_INCLUDE_DIRS}
            ${CMAKE_SOURCE_DIR}/backend/signaling_server/src)
        target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
        target_link_libraries(${name} PRIVATE
            GTest::gtest_main driscord_core driscord_signaling)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endfunction()

    add_integration_test(test_room_isolation)
    add_integration_test(test_google_webrtc_transport)
    target_link_libraries(test_google_webrtc_transport PRIVATE
        driscord_webrtc_backend)
    add_integration_test(test_google_webrtc_screen_transport)
    target_link_libraries(test_google_webrtc_screen_transport PRIVATE
        driscord_webrtc_backend)
endif()
