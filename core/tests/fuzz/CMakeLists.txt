# libFuzzer harnesses over the SFU's parsing/rewrite surface. Built only under
# the server-fuzz preset (DRISCORD_FUZZ=ON, BUILD_CORE=OFF): every target in
# the tree is already instrumented with fuzzer coverage + ASan/UBSan by the
# root CMakeLists; the executables here only add the libFuzzer driver.
#
# Each fuzzer is also registered as a ctest case that replays its checked-in
# corpus with -runs=0. That regression pass takes seconds and runs in the PR
# gate; open-ended fuzzing (-max_total_time) is the nightly's job.

function(add_fuzz_target name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src
        ${CMAKE_SOURCE_DIR}/core/src/utils
        ${CMAKE_SOURCE_DIR}/backend/signaling_server/src)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(${name} PRIVATE ${ARGN})
endfunction()

function(add_fuzz_regression name corpus)
    add_test(NAME ${name}_corpus
        COMMAND ${name} -runs=0
            ${CMAKE_CURRENT_SOURCE_DIR}/corpora/${corpus})
    set_tests_properties(${name}_corpus PROPERTIES TIMEOUT 120 LABELS "fuzz")
endfunction()

add_fuzz_target(fuzz_rtp_slot_rewriter driscord_signaling)
add_fuzz_regression(fuzz_rtp_slot_rewriter rtp)

add_fuzz_target(fuzz_sfu_sdp driscord_signaling)
add_fuzz_regression(fuzz_sfu_sdp sdp)

add_fuzz_target(fuzz_protocol driscord_protocol)
add_fuzz_regression(fuzz_protocol signaling)
