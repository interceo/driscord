# Validate the staged component tree before CPack creates the archive. Keeping
# the complete gate here makes a failure prevent artifact creation; CPack post
# scripts run too late to provide that guarantee.

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
    "driscord"
    "README.txt"
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

foreach(_driscord_forbidden_path IN ITEMS config.json include lib/cmake)
    if(EXISTS "${_driscord_package_root}/${_driscord_forbidden_path}")
        message(FATAL_ERROR
            "Development files leaked into Runtime component: "
            "${_driscord_forbidden_path}")
    endif()
endforeach()

find_program(_driscord_readelf NAMES readelf REQUIRED)
find_program(_driscord_ldd NAMES ldd REQUIRED)

execute_process(
    COMMAND "${_driscord_package_root}/driscord" --version
    RESULT_VARIABLE _driscord_version_result
    OUTPUT_VARIABLE _driscord_version_output
    ERROR_VARIABLE _driscord_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _driscord_version_result EQUAL 0
        OR NOT _driscord_version_output MATCHES
            "^Driscord [0-9]+\\.[0-9]+\\.[0-9]+([+.-][0-9A-Za-z.+-]+)?$")
    message(FATAL_ERROR
        "Packaged launcher failed its version probe: "
        "${_driscord_version_output}${_driscord_version_error}")
endif()

file(GLOB_RECURSE _driscord_package_files LIST_DIRECTORIES FALSE
    "${_driscord_package_root}/*")
set(_driscord_elf_files)
foreach(_driscord_candidate IN LISTS _driscord_package_files)
    if(NOT IS_SYMLINK "${_driscord_candidate}")
        execute_process(
            COMMAND "${_driscord_readelf}" -h "${_driscord_candidate}"
            RESULT_VARIABLE _driscord_readelf_result
            OUTPUT_QUIET ERROR_QUIET)
        if(_driscord_readelf_result EQUAL 0)
            list(APPEND _driscord_elf_files "${_driscord_candidate}")
        endif()
    endif()
endforeach()
list(LENGTH _driscord_elf_files _driscord_elf_count)
if(_driscord_elf_count EQUAL 0)
    message(FATAL_ERROR "Runtime component contains no ELF files")
endif()

execute_process(
    COMMAND "${_driscord_readelf}" -d ${_driscord_elf_files}
    RESULT_VARIABLE _driscord_dynamic_result
    OUTPUT_VARIABLE _driscord_dynamic
    ERROR_VARIABLE _driscord_dynamic_error)
if(NOT _driscord_dynamic_result EQUAL 0)
    message(FATAL_ERROR "readelf -d failed: ${_driscord_dynamic_error}")
endif()
if(_driscord_dynamic MATCHES
        "(RPATH|RUNPATH)[^\n]*(/opt/qt|/source|/ci/|\\.builds|/mnt/raid1)")
    message(FATAL_ERROR "Build path leaked into packaged RPATH/RUNPATH")
endif()

execute_process(
    COMMAND "${_driscord_readelf}" -d "${_driscord_executable}"
    RESULT_VARIABLE _driscord_client_dynamic_result
    OUTPUT_VARIABLE _driscord_client_dynamic
    ERROR_VARIABLE _driscord_client_dynamic_error)
if(NOT _driscord_client_dynamic_result EQUAL 0
        OR NOT _driscord_client_dynamic MATCHES
            "Library runpath: \\[\\$ORIGIN:\\$ORIGIN/../lib\\]")
    message(FATAL_ERROR
        "Unexpected RUNPATH for bin/driscord_client: "
        "${_driscord_client_dynamic_error}${_driscord_client_dynamic}")
endif()

execute_process(
    COMMAND "${_driscord_ldd}" ${_driscord_elf_files}
    RESULT_VARIABLE _driscord_ldd_result
    OUTPUT_VARIABLE _driscord_ldd_output
    ERROR_VARIABLE _driscord_ldd_error)
if(NOT _driscord_ldd_result EQUAL 0)
    message(FATAL_ERROR
        "ldd failed for packaged ELF files: "
        "${_driscord_ldd_output}${_driscord_ldd_error}")
endif()
if(_driscord_ldd_output MATCHES "not found")
    message(FATAL_ERROR "Unresolved dependency in package: ${_driscord_ldd_output}")
endif()
# Resolutions through $ORIGIN legitimately contain the staging path. Remove
# that prefix before checking whether anything resolves back to the build host.
string(REPLACE "${_driscord_package_root}" "<PACKAGE>"
    _driscord_external_ldd_output "${_driscord_ldd_output}")
if(_driscord_external_ldd_output MATCHES
        "(/opt/qt|/source|/ci/|\\.builds|/mnt/raid1)")
    message(FATAL_ERROR "Build-host dependency leaked into package")
endif()

execute_process(
    COMMAND "${_driscord_readelf}" --version-info ${_driscord_elf_files}
    RESULT_VARIABLE _driscord_version_info_result
    OUTPUT_VARIABLE _driscord_version_info
    ERROR_VARIABLE _driscord_version_info_error)
if(NOT _driscord_version_info_result EQUAL 0)
    message(FATAL_ERROR
        "readelf --version-info failed: ${_driscord_version_info_error}")
endif()

function(_driscord_max_symbol _prefix _output)
    string(REGEX MATCHALL "${_prefix}_[0-9]+(\\.[0-9]+)+"
        _driscord_symbols "${_driscord_version_info}")
    list(REMOVE_DUPLICATES _driscord_symbols)
    list(SORT _driscord_symbols COMPARE NATURAL ORDER DESCENDING)
    if(_driscord_symbols)
        list(GET _driscord_symbols 0 _driscord_symbol)
    else()
        set(_driscord_symbol "")
    endif()
    set(${_output} "${_driscord_symbol}" PARENT_SCOPE)
endfunction()

_driscord_max_symbol("GLIBC" _driscord_max_glibc)
_driscord_max_symbol("GLIBCXX" _driscord_max_glibcxx)
if(_driscord_max_glibc STREQUAL "" OR _driscord_max_glibcxx STREQUAL "")
    message(FATAL_ERROR "Package has no GLIBC or GLIBCXX symbol requirements")
endif()
string(REPLACE "GLIBC_" "" _driscord_glibc_version "${_driscord_max_glibc}")
string(REPLACE "GLIBCXX_" "" _driscord_glibcxx_version "${_driscord_max_glibcxx}")
if(_driscord_glibc_version VERSION_GREATER "2.34")
    message(FATAL_ERROR "GLIBC floor exceeded: ${_driscord_max_glibc} > GLIBC_2.34")
endif()
if(_driscord_glibcxx_version VERSION_GREATER "3.4.30")
    message(FATAL_ERROR
        "GLIBCXX floor exceeded: ${_driscord_max_glibcxx} > GLIBCXX_3.4.30")
endif()

message(STATUS
    "Verified staged Driscord Runtime: ${_driscord_elf_count} ELF files, "
    "${_driscord_max_glibc}, ${_driscord_max_glibcxx}, "
    "${_driscord_version_output}")
