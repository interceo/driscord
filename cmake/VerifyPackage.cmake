# Validate the staged component tree before CPack creates the archive. This is
# deliberately a structural check; ELF resolution and symbol floors are
# checked against the finished archive by ci/check-package.sh.

if(NOT DEFINED CPACK_TEMPORARY_INSTALL_DIRECTORY
        OR CPACK_TEMPORARY_INSTALL_DIRECTORY STREQUAL "")
    message(FATAL_ERROR
        "CPACK_TEMPORARY_INSTALL_DIRECTORY is unavailable in the pre-build script")
endif()

file(GLOB_RECURSE _driscord_packaged_executables
    LIST_DIRECTORIES FALSE
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*/bin/driscord_client"
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/bin/driscord_client")
list(LENGTH _driscord_packaged_executables _driscord_executable_count)
if(NOT _driscord_executable_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one staged bin/driscord_client, found "
        "${_driscord_executable_count} below "
        "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()

list(GET _driscord_packaged_executables 0 _driscord_executable)
get_filename_component(_driscord_bin_dir "${_driscord_executable}" DIRECTORY)
get_filename_component(_driscord_package_root "${_driscord_bin_dir}" DIRECTORY)

set(_driscord_required_paths
    "lib/libQt6Widgets.so.6"
    "lib/libQt6Core5Compat.so.6"
    "plugins/platforms/libqoffscreen.so"
    "plugins/platforms/libqminimal.so")
foreach(_driscord_relative_path IN LISTS _driscord_required_paths)
    if(NOT EXISTS "${_driscord_package_root}/${_driscord_relative_path}")
        message(FATAL_ERROR
            "Required runtime artifact is missing: ${_driscord_relative_path}")
    endif()
endforeach()

file(GLOB _driscord_datachannel_libraries
    LIST_DIRECTORIES FALSE
    "${_driscord_package_root}/lib/libdatachannel.so*")
if(NOT _driscord_datachannel_libraries)
    message(FATAL_ERROR "Required runtime artifact is missing: lib/libdatachannel.so*")
endif()

foreach(_driscord_forbidden_path IN ITEMS include lib/cmake)
    if(EXISTS "${_driscord_package_root}/${_driscord_forbidden_path}")
        message(FATAL_ERROR
            "Development files leaked into Runtime component: "
            "${_driscord_forbidden_path}")
    endif()
endforeach()

message(STATUS "Verified staged Driscord Runtime component: ${_driscord_package_root}")
