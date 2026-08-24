include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
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
    set_tests_properties(${name} PROPERTIES LABELS "unit")
endfunction()

# --- Helper for tests that need the Google WebRTC client media stack ---
function(add_core_test name)
    add_executable(${name} unit/${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main driscord_core Boost::boost)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "unit;media")
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
set_tests_properties(test_protocol PROPERTIES LABELS "unit")

if(DRISCORD_FUZZ AND BUILD_SERVER)
    add_subdirectory(fuzz)
endif()

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
        set_tests_properties(${name} PROPERTIES LABELS "unit;signaling")
    endfunction()

    add_signaling_test(test_rtp_slot_rewriter unit/test_rtp_slot_rewriter.cpp)
    add_signaling_test(test_sfu_media_utils unit/test_sfu_media_utils.cpp)
    add_signaling_test(test_sfu_rtcp_classification
        unit/test_sfu_rtcp_classification.cpp)
    add_signaling_test(test_rtp_burst_loss unit/test_rtp_burst_loss.cpp)

    # Raw-Beast hostile input against the live server: garbage frames,
    # oversized messages, aborted sockets, hostile usernames, malformed SDP.
    add_signaling_test(test_ws_server_hostile
        integration/test_ws_server_hostile.cpp)
    target_link_libraries(test_ws_server_hostile PRIVATE
        ${DRISCORD_LIBDATACHANNEL_TARGET})
    set_tests_properties(test_ws_server_hostile PROPERTIES TIMEOUT 90
        LABELS "integration;signaling")

    # The authenticator against an API that is down, slow, broken or lying:
    # every failure mode must fail closed without hanging the io_context.
    add_signaling_test(test_api_authenticator
        integration/test_api_authenticator.cpp)
    set_tests_properties(test_api_authenticator PROPERTIES TIMEOUT 60
        LABELS "integration;signaling")

    # Real WebSocket handshakes against the real server, but no media: this is
    # the gate on session authorization.
    add_signaling_test(test_signaling_auth integration/test_signaling_auth.cpp)
    target_link_libraries(test_signaling_auth PRIVATE
        ${DRISCORD_LIBDATACHANNEL_TARGET})
    set_tests_properties(test_signaling_auth PROPERTIES TIMEOUT 60
        LABELS "integration;signaling")
endif()

if(BUILD_CORE)
    add_core_test(test_google_webrtc_screen_stats)

    # Vendored WebRTC test helpers (third_party/webrtc_test/README.md): the
    # pinned archive is built with rtc_include_tests=false, so upstream test/
    # sources come from git while everything they call is production code
    # already inside libwebrtc.a. SYSTEM include and no warning flags: the
    # copies stay byte-identical to upstream.
    add_library(driscord_webrtc_test_support STATIC
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test/test/rtcp_packet_parser.cc
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test/test/rtp_file_reader.cc
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test/test/rtp_file_writer.cc)
    target_include_directories(driscord_webrtc_test_support SYSTEM PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test)
    target_compile_options(driscord_webrtc_test_support PRIVATE -fno-rtti)
    # Headers/defines only — deliberately NOT target_link_libraries: pulling
    # libwebrtc.a in through this helper reorders it after the OpenSSL DSOs
    # in binaries that also link driscord_signaling, and the archive's
    # BoringSSL then resolves against system libcrypto and crashes (the ABI
    # hazard CLAUDE.md documents). Consumers must link the archive themselves
    # via driscord_webrtc_backend or driscord::google_webrtc, which keeps it
    # ahead of the OpenSSL DSOs.
    target_include_directories(driscord_webrtc_test_support SYSTEM PRIVATE
        $<TARGET_PROPERTY:driscord_google_webrtc,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(driscord_webrtc_test_support PRIVATE
        $<TARGET_PROPERTY:driscord_google_webrtc,INTERFACE_COMPILE_DEFINITIONS>)

    # Replays the committed real-traffic fixtures through the slot rewriter.
    add_executable(test_rtpdump_replay unit/test_rtpdump_replay.cpp)
    target_include_directories(test_rtpdump_replay PRIVATE
        ${TEST_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/backend/signaling_server/src)
    target_compile_definitions(test_rtpdump_replay PRIVATE
        DRISCORD_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    target_compile_options(test_rtpdump_replay PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_rtpdump_replay PRIVATE
        GTest::gtest_main driscord_signaling driscord_webrtc_test_support
        driscord::google_webrtc)
    add_test(NAME test_rtpdump_replay COMMAND test_rtpdump_replay)
    set_tests_properties(test_rtpdump_replay PROPERTIES TIMEOUT 30
        LABELS "unit;media")

    add_executable(test_rtpdump_roundtrip unit/test_rtpdump_roundtrip.cpp)
    target_compile_options(test_rtpdump_roundtrip PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_rtpdump_roundtrip PRIVATE
        GTest::gtest_main driscord_webrtc_test_support
        driscord::google_webrtc)
    add_test(NAME test_rtpdump_roundtrip COMMAND test_rtpdump_roundtrip)
    set_tests_properties(test_rtpdump_roundtrip PROPERTIES TIMEOUT 30
        LABELS "unit;media")

    # The PCM playout is a small self-contained hardware boundary (miniaudio +
    # boost lockfree, no Google WebRTC), so its test compiles the production
    # sources straight in rather than linking driscord_core.
    add_executable(test_pcm_playout
        unit/test_pcm_playout.cpp
        ${CMAKE_SOURCE_DIR}/core/src/webrtc/google_webrtc_pcm_playout.cpp
        ${CMAKE_SOURCE_DIR}/core/src/utils/ma_device.cpp)
    target_include_directories(test_pcm_playout PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_pcm_playout PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_pcm_playout PRIVATE
        GTest::gtest_main miniaudio_lib Boost::boost)
    add_test(NAME test_pcm_playout COMMAND test_pcm_playout)
    set_tests_properties(test_pcm_playout PROPERTIES LABELS "unit;media")

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
    set_tests_properties(test_libdatachannel_wss PROPERTIES TIMEOUT 30
        LABELS "integration;media")

    add_executable(test_google_webrtc_runtime
        unit/test_google_webrtc_runtime.cpp)
    target_include_directories(test_google_webrtc_runtime PRIVATE
        ${TEST_INCLUDE_DIRS})
    target_compile_options(test_google_webrtc_runtime PRIVATE
        ${TEST_WARNING_FLAGS})
    target_link_libraries(test_google_webrtc_runtime PRIVATE
        GTest::gtest_main driscord_webrtc_backend)
    add_test(NAME test_google_webrtc_runtime COMMAND test_google_webrtc_runtime)
    set_tests_properties(test_google_webrtc_runtime PROPERTIES TIMEOUT 30
        LABELS "unit;media")
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
        set_tests_properties(${name} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
            LABELS "integration;media")
    endfunction()

    add_integration_test(test_room_isolation)
    add_integration_test(test_shutdown_ordering)
    add_integration_test(test_reconnect)
    add_integration_test(test_client_lifecycle)
    target_link_libraries(test_client_lifecycle PRIVATE driscord_webrtc_backend)
    add_integration_test(test_google_webrtc_transport)
    target_link_libraries(test_google_webrtc_transport PRIVATE
        driscord_webrtc_backend)
    add_integration_test(test_google_webrtc_screen_transport)
    target_link_libraries(test_google_webrtc_screen_transport PRIVATE
        driscord_webrtc_backend)
    # Captures real publisher RTP off the fault-stage tap into rtpdump; with
    # DRISCORD_RECORD_FIXTURES_DIR set it refreshes core/tests/fixtures/.
    add_integration_test(test_rtpdump_capture)
    target_link_libraries(test_rtpdump_capture PRIVATE
        driscord_webrtc_backend driscord_webrtc_test_support)
endif()
