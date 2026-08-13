# Run the full ELF/dependency check after CPack has created the archive. Keeping
# this in CPack makes `cmake --workflow --preset linux-release` the complete
# release gate instead of relying on a separate CI step that can be forgotten.

if(NOT DEFINED CPACK_PACKAGE_FILES OR NOT CPACK_PACKAGE_FILES)
    message(FATAL_ERROR "CPack did not expose any generated package files")
endif()

list(LENGTH CPACK_PACKAGE_FILES _driscord_package_count)
if(NOT _driscord_package_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one generated package, found ${_driscord_package_count}: "
        "${CPACK_PACKAGE_FILES}")
endif()

list(GET CPACK_PACKAGE_FILES 0 _driscord_package)
get_filename_component(_driscord_checker
    "${CMAKE_CURRENT_LIST_DIR}/../ci/check-package.sh" ABSOLUTE)
if(NOT EXISTS "${_driscord_checker}")
    message(FATAL_ERROR "Package checker not found: ${_driscord_checker}")
endif()

execute_process(
    COMMAND "${_driscord_checker}" "${_driscord_package}"
    RESULT_VARIABLE _driscord_check_result
    OUTPUT_VARIABLE _driscord_check_stdout
    ERROR_VARIABLE _driscord_check_stderr)

if(NOT _driscord_check_result EQUAL 0)
    message(FATAL_ERROR
        "Generated package failed validation (${_driscord_check_result})\n"
        "${_driscord_check_stdout}${_driscord_check_stderr}")
endif()

string(STRIP "${_driscord_check_stdout}" _driscord_check_stdout)
message(STATUS "${_driscord_check_stdout}")
