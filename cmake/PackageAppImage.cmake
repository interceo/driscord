
cmake_policy(VERSION 3.25)

foreach(_driscord_required_variable IN ITEMS
        CPACK_TEMPORARY_DIRECTORY
        CPACK_TOPLEVEL_DIRECTORY
        CPACK_PACKAGE_FILE_NAME
        CPACK_DRISCORD_APPIMAGE_RUNTIME)
    if(NOT DEFINED ${_driscord_required_variable}
            OR "${${_driscord_required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "${_driscord_required_variable} is unavailable in the AppImage "
            "package script")
    endif()
endforeach()

file(READ "${CPACK_DRISCORD_APPIMAGE_RUNTIME}" _driscord_runtime_magic
    OFFSET 8 LIMIT 3 HEX)
if(NOT _driscord_runtime_magic STREQUAL "414902")
    message(FATAL_ERROR
        "Not a type-2 AppImage runtime (missing AI\\x02 magic): "
        "${CPACK_DRISCORD_APPIMAGE_RUNTIME}")
endif()

file(GLOB_RECURSE _driscord_staged_executables LIST_DIRECTORIES FALSE
    "${CPACK_TEMPORARY_DIRECTORY}/*/bin/driscord_client"
    "${CPACK_TEMPORARY_DIRECTORY}/bin/driscord_client")
list(LENGTH _driscord_staged_executables _driscord_staged_count)
if(NOT _driscord_staged_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one staged bin/driscord_client, found "
        "${_driscord_staged_count} below ${CPACK_TEMPORARY_DIRECTORY}")
endif()
list(GET _driscord_staged_executables 0 _driscord_staged_executable)
get_filename_component(_driscord_bin_dir
    "${_driscord_staged_executable}" DIRECTORY)
get_filename_component(_driscord_appdir "${_driscord_bin_dir}" DIRECTORY)

find_program(_driscord_mksquashfs NAMES mksquashfs REQUIRED)

set(_driscord_squashfs "${CPACK_TOPLEVEL_DIRECTORY}/payload.squashfs")
file(REMOVE "${_driscord_squashfs}")
execute_process(
    COMMAND "${_driscord_mksquashfs}" "${_driscord_appdir}"
        "${_driscord_squashfs}"
        -comp zstd -Xcompression-level 19
        -root-owned -noappend -no-xattrs -quiet
    RESULT_VARIABLE _driscord_mksquashfs_result
    ERROR_VARIABLE _driscord_mksquashfs_error)
if(NOT _driscord_mksquashfs_result EQUAL 0)
    message(FATAL_ERROR "mksquashfs failed: ${_driscord_mksquashfs_error}")
endif()

set(_driscord_appimage
    "${CPACK_TOPLEVEL_DIRECTORY}/${CPACK_PACKAGE_FILE_NAME}.AppImage")
file(REMOVE "${_driscord_appimage}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E cat
        "${CPACK_DRISCORD_APPIMAGE_RUNTIME}" "${_driscord_squashfs}"
    OUTPUT_FILE "${_driscord_appimage}"
    RESULT_VARIABLE _driscord_cat_result
    ERROR_VARIABLE _driscord_cat_error)
if(NOT _driscord_cat_result EQUAL 0)
    message(FATAL_ERROR
        "Concatenating runtime and squashfs failed: ${_driscord_cat_error}")
endif()
file(REMOVE "${_driscord_squashfs}")
file(CHMOD "${_driscord_appimage}" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

set(_driscord_smoke_dir "${CPACK_TOPLEVEL_DIRECTORY}/appimage-smoke")
file(REMOVE_RECURSE "${_driscord_smoke_dir}")
file(MAKE_DIRECTORY "${_driscord_smoke_dir}")
execute_process(
    COMMAND "${_driscord_appimage}" --appimage-extract
    WORKING_DIRECTORY "${_driscord_smoke_dir}"
    RESULT_VARIABLE _driscord_extract_result
    OUTPUT_QUIET
    ERROR_VARIABLE _driscord_extract_error)
if(NOT _driscord_extract_result EQUAL 0)
    message(FATAL_ERROR
        "AppImage self-extraction failed: ${_driscord_extract_error}")
endif()
execute_process(
    COMMAND "${_driscord_smoke_dir}/squashfs-root/AppRun" --version
    RESULT_VARIABLE _driscord_probe_result
    OUTPUT_VARIABLE _driscord_probe_output
    ERROR_VARIABLE _driscord_probe_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _driscord_probe_result EQUAL 0
        OR NOT _driscord_probe_output MATCHES "^Driscord [0-9]+\\.")
    message(FATAL_ERROR
        "Extracted AppImage failed its version probe: "
        "${_driscord_probe_output}${_driscord_probe_error}")
endif()
file(REMOVE_RECURSE "${_driscord_smoke_dir}")

message(STATUS "Assembled ${_driscord_appimage} (${_driscord_probe_output})")
set(CPACK_EXTERNAL_BUILT_PACKAGES "${_driscord_appimage}")
