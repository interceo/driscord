include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    GIT_SHALLOW    TRUE
)

set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(googletest)

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

function(add_unit_test name)
    add_executable(${name} unit/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main Boost::boost)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "unit")
endfunction()

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

add_unit_test(test_media_metrics)
set_tests_properties(test_media_metrics PROPERTIES LABELS "unit;media")

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
    add_signaling_test(test_link_model unit/test_link_model.cpp)

    add_signaling_test(test_ws_server_hostile
        integration/test_ws_server_hostile.cpp)
    target_link_libraries(test_ws_server_hostile PRIVATE
        ${DRISCORD_LIBDATACHANNEL_TARGET})
    set_tests_properties(test_ws_server_hostile PROPERTIES TIMEOUT 90
        LABELS "integration;signaling")

    add_signaling_test(test_api_authenticator
        integration/test_api_authenticator.cpp)
    set_tests_properties(test_api_authenticator PROPERTIES TIMEOUT 60
        LABELS "integration;signaling")

    add_signaling_test(test_signaling_auth integration/test_signaling_auth.cpp)
    target_link_libraries(test_signaling_auth PRIVATE
        ${DRISCORD_LIBDATACHANNEL_TARGET})
    set_tests_properties(test_signaling_auth PROPERTIES TIMEOUT 60
        LABELS "integration;signaling")
endif()

if(BUILD_CORE)
    add_core_test(test_google_webrtc_screen_stats)

    add_library(driscord_test_media_support STATIC
        support/frame_marker.cpp
        support/video_quality.cpp
        support/screen_content.cpp
        support/audio_probe.cpp
        support/media_dump.cpp)
    target_include_directories(driscord_test_media_support PUBLIC
        ${TEST_INCLUDE_DIRS})
    target_compile_options(driscord_test_media_support PRIVATE
        ${TEST_WARNING_FLAGS})
    target_include_directories(driscord_test_media_support SYSTEM PRIVATE
        $<TARGET_PROPERTY:driscord_google_webrtc,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(driscord_test_media_support PRIVATE
        $<TARGET_PROPERTY:driscord_google_webrtc,INTERFACE_COMPILE_DEFINITIONS>)

    add_executable(test_frame_marker unit/test_frame_marker.cpp)
    target_compile_options(test_frame_marker PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_frame_marker PRIVATE
        GTest::gtest_main driscord_test_media_support driscord::google_webrtc)
    add_test(NAME test_frame_marker COMMAND test_frame_marker)
    set_tests_properties(test_frame_marker PROPERTIES TIMEOUT 30
        LABELS "unit;media")

    add_executable(test_video_quality_accumulator
        unit/test_video_quality_accumulator.cpp)
    target_compile_options(test_video_quality_accumulator PRIVATE
        ${TEST_WARNING_FLAGS})
    target_link_libraries(test_video_quality_accumulator PRIVATE
        GTest::gtest_main driscord_test_media_support driscord::google_webrtc)
    add_test(NAME test_video_quality_accumulator
        COMMAND test_video_quality_accumulator)
    set_tests_properties(test_video_quality_accumulator PROPERTIES TIMEOUT 30
        LABELS "unit;media")

    add_executable(test_chirp_detector unit/test_chirp_detector.cpp)
    target_compile_options(test_chirp_detector PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_chirp_detector PRIVATE
        GTest::gtest_main driscord_test_media_support)
    add_test(NAME test_chirp_detector COMMAND test_chirp_detector)
    set_tests_properties(test_chirp_detector PROPERTIES TIMEOUT 30
        LABELS "unit;media")

    add_executable(test_media_dump unit/test_media_dump.cpp)
    target_compile_options(test_media_dump PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_media_dump PRIVATE
        GTest::gtest_main driscord_test_media_support)
    add_test(NAME test_media_dump COMMAND test_media_dump)
    set_tests_properties(test_media_dump PROPERTIES TIMEOUT 30
        LABELS "unit;media")

    add_library(driscord_webrtc_test_support STATIC
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test/test/rtcp_packet_parser.cc
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test/test/rtp_file_reader.cc
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test/test/rtp_file_writer.cc)
    target_include_directories(driscord_webrtc_test_support SYSTEM PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party/webrtc_test)
    target_compile_options(driscord_webrtc_test_support PRIVATE -fno-rtti)
    target_include_directories(driscord_webrtc_test_support SYSTEM PUBLIC
        $<TARGET_PROPERTY:driscord_google_webrtc,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(driscord_webrtc_test_support PUBLIC
        $<TARGET_PROPERTY:driscord_google_webrtc,INTERFACE_COMPILE_DEFINITIONS>)

    add_executable(test_rtpdump_roundtrip unit/test_rtpdump_roundtrip.cpp)
    target_compile_options(test_rtpdump_roundtrip PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(test_rtpdump_roundtrip PRIVATE
        GTest::gtest_main driscord_webrtc_test_support
        driscord::google_webrtc)
    add_test(NAME test_rtpdump_roundtrip COMMAND test_rtpdump_roundtrip)
    set_tests_properties(test_rtpdump_roundtrip PROPERTIES TIMEOUT 30
        LABELS "unit;media")

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

if(BUILD_SERVER AND BUILD_CORE)
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
    add_integration_test(test_google_webrtc_degraded_voice)
    target_link_libraries(test_google_webrtc_degraded_voice PRIVATE
        driscord_webrtc_backend)
    add_integration_test(test_google_webrtc_degraded_screen)
    target_link_libraries(test_google_webrtc_degraded_screen PRIVATE
        driscord_webrtc_backend)
    add_integration_test(test_google_webrtc_network_scenarios)
    target_link_libraries(test_google_webrtc_network_scenarios PRIVATE
        driscord_webrtc_backend)
    add_integration_test(test_google_webrtc_screen_quality)
    target_link_libraries(test_google_webrtc_screen_quality PRIVATE
        driscord_webrtc_backend driscord_test_media_support)
    add_integration_test(test_google_webrtc_av_sync)
    target_link_libraries(test_google_webrtc_av_sync PRIVATE
        driscord_webrtc_backend driscord_test_media_support)
    add_integration_test(test_rtpdump_capture)
    target_link_libraries(test_rtpdump_capture PRIVATE
        driscord_webrtc_backend driscord_webrtc_test_support)

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
endif()
