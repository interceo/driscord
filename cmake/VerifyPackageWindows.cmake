# Validate the staged Windows component tree before CPack creates the
# archive, mirroring cmake/VerifyPackage.cmake for the Linux TGZ. The deploy
# closure in client-qt/CMakeLists.txt is what makes the package complete;
# this gate re-checks the result independently so a deploy regression fails
# the package build instead of shipping a zip that cannot start.

if(NOT DEFINED CPACK_TEMPORARY_INSTALL_DIRECTORY
        OR CPACK_TEMPORARY_INSTALL_DIRECTORY STREQUAL "")
    message(FATAL_ERROR
        "CPACK_TEMPORARY_INSTALL_DIRECTORY is unavailable in the pre-build script")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/WindowsSystemDlls.cmake")

file(GLOB_RECURSE _driscord_packaged_executables
    LIST_DIRECTORIES FALSE
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*/driscord_client.exe"
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/driscord_client.exe")
list(LENGTH _driscord_packaged_executables _driscord_executable_count)
if(NOT _driscord_executable_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one staged driscord_client.exe, found "
        "${_driscord_executable_count} below "
        "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()

list(GET _driscord_packaged_executables 0 _driscord_executable)
get_filename_component(_driscord_package_root "${_driscord_executable}" DIRECTORY)

set(_driscord_required_paths
    "qt.conf"
    "Qt6Core.dll"
    "Qt6Gui.dll"
    "Qt6Quick.dll"
    "msvcp140.dll"
    "vcruntime140.dll"
    "vcruntime140_1.dll"
    "plugins/platforms/qwindows.dll"
    "plugins/styles/qmodernwindowsstyle.dll"
    "plugins/tls/qschannelbackend.dll"
    "plugins/iconengines/qsvgicon.dll"
    "qml/QtQuick/qmldir"
    "qml/QtQuick/Controls/qmldir"
    "qml/Qt5Compat/GraphicalEffects/qmldir")
foreach(_driscord_relative_path IN LISTS _driscord_required_paths)
    if(NOT EXISTS "${_driscord_package_root}/${_driscord_relative_path}")
        message(FATAL_ERROR
            "Required runtime artifact is missing: ${_driscord_relative_path}")
    endif()
endforeach()

foreach(_driscord_forbidden_path IN ITEMS include lib/cmake)
    if(EXISTS "${_driscord_package_root}/${_driscord_forbidden_path}")
        message(FATAL_ERROR
            "Development files leaked into Runtime component: "
            "${_driscord_forbidden_path}")
    endif()
endforeach()

file(GLOB_RECURSE _driscord_debug_leftovers LIST_DIRECTORIES FALSE
    "${_driscord_package_root}/*.pdb")
if(_driscord_debug_leftovers)
    message(FATAL_ERROR
        "Debug files leaked into the package: ${_driscord_debug_leftovers}")
endif()

find_program(_driscord_objdump NAMES llvm-objdump objdump REQUIRED)

file(GLOB_RECURSE _driscord_package_files LIST_DIRECTORIES FALSE
    "${_driscord_package_root}/*.dll" "${_driscord_package_root}/*.exe")

# Names shipped inside the package, lowercased: PE import references are
# case-insensitive.
set(_driscord_shipped_names "")
foreach(_driscord_packaged_file IN LISTS _driscord_package_files)
    get_filename_component(_driscord_name "${_driscord_packaged_file}" NAME)
    string(TOLOWER "${_driscord_name}" _driscord_name)
    list(APPEND _driscord_shipped_names "${_driscord_name}")
endforeach()

set(_driscord_pe_count 0)
foreach(_driscord_packaged_file IN LISTS _driscord_package_files)
    execute_process(
        COMMAND "${_driscord_objdump}" -p "${_driscord_packaged_file}"
        RESULT_VARIABLE _driscord_objdump_result
        OUTPUT_VARIABLE _driscord_objdump_output
        ERROR_VARIABLE _driscord_objdump_error)
    if(NOT _driscord_objdump_result EQUAL 0)
        message(FATAL_ERROR
            "objdump failed for ${_driscord_packaged_file}: "
            "${_driscord_objdump_error}")
    endif()
    math(EXPR _driscord_pe_count "${_driscord_pe_count} + 1")
    string(REGEX MATCHALL "DLL Name: [^\n]+" _driscord_imports
        "${_driscord_objdump_output}")
    foreach(_driscord_import IN LISTS _driscord_imports)
        string(REGEX REPLACE "^DLL Name: *" "" _driscord_import
            "${_driscord_import}")
        string(STRIP "${_driscord_import}" _driscord_import)
        string(TOLOWER "${_driscord_import}" _driscord_import)
        if(NOT _driscord_import IN_LIST _driscord_shipped_names
                AND NOT _driscord_import MATCHES
                    "${DRISCORD_WINDOWS_SYSTEM_DLL_REGEX}")
            message(FATAL_ERROR
                "${_driscord_packaged_file} imports ${_driscord_import}, "
                "which is neither packaged nor an operating-system DLL")
        endif()
    endforeach()
endforeach()
if(_driscord_pe_count EQUAL 0)
    message(FATAL_ERROR "Runtime component contains no PE files")
endif()

message(STATUS
    "Verified staged Driscord Windows Runtime: ${_driscord_pe_count} PE "
    "files, imports closed over the package and the OS allowlist")
