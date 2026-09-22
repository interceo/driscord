
cmake_policy(VERSION 3.25)

if(NOT DEFINED CPACK_TEMPORARY_INSTALL_DIRECTORY
        OR CPACK_TEMPORARY_INSTALL_DIRECTORY STREQUAL "")
    message(FATAL_ERROR
        "CPACK_TEMPORARY_INSTALL_DIRECTORY is unavailable in the pre-build script")
endif()
foreach(_driscord_tool CPACK_DRISCORD_LIPO CPACK_DRISCORD_OTOOL)
    if("${${_driscord_tool}}" STREQUAL "")
        message(FATAL_ERROR "${_driscord_tool} is not set")
    endif()
endforeach()

file(GLOB_RECURSE _driscord_packaged_executables
    LIST_DIRECTORIES FALSE
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*/Driscord.app/Contents/MacOS/driscord_client"
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/Driscord.app/Contents/MacOS/driscord_client")
list(LENGTH _driscord_packaged_executables _driscord_executable_count)
if(NOT _driscord_executable_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one staged Driscord.app, found "
        "${_driscord_executable_count} below "
        "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()
list(GET _driscord_packaged_executables 0 _driscord_executable)
get_filename_component(_driscord_contents "${_driscord_executable}" DIRECTORY)
get_filename_component(_driscord_contents "${_driscord_contents}" DIRECTORY)
get_filename_component(_driscord_bundle "${_driscord_contents}" DIRECTORY)
get_filename_component(_driscord_package_root "${_driscord_bundle}" DIRECTORY)

# The archive is unpacked straight into /Applications or wherever the user
# keeps apps, and the updater moves each top-level entry into place: anything
# beside the bundle would land there too.
file(GLOB _driscord_top_level "${_driscord_package_root}/*")
if(NOT _driscord_top_level STREQUAL _driscord_bundle)
    message(FATAL_ERROR
        "The package root must hold Driscord.app alone, found: "
        "${_driscord_top_level}")
endif()

set(_driscord_required_paths
    "Info.plist"
    "PkgInfo"
    "Resources/qt.conf"
    "Resources/driscord.icns"
    "Frameworks/QtCore.framework/Versions/A/QtCore"
    "Frameworks/QtCore.framework/Versions/A/Resources/Info.plist"
    "Frameworks/QtGui.framework/Versions/A/QtGui"
    "Frameworks/QtQuick.framework/Versions/A/QtQuick"
    "Frameworks/QtMultimedia.framework/Versions/A/QtMultimedia"
    "PlugIns/platforms/libqcocoa.dylib"
    "PlugIns/styles/libqmacstyle.dylib"
    "PlugIns/tls/libqsecuretransportbackend.dylib"
    "PlugIns/iconengines/libqsvgicon.dylib"
    "PlugIns/multimedia/libffmpegmediaplugin.dylib"
    "Resources/qml/QtQuick/qmldir"
    "Resources/qml/QtQuick/Controls/qmldir"
    "Resources/qml/QtQuick/Controls/Basic/qmldir"
    "Resources/qml/QtQuick/Effects/qmldir")
foreach(_driscord_relative_path IN LISTS _driscord_required_paths)
    if(NOT EXISTS "${_driscord_contents}/${_driscord_relative_path}")
        message(FATAL_ERROR
            "Required runtime artifact is missing: "
            "Driscord.app/Contents/${_driscord_relative_path}")
    endif()
endforeach()

file(READ "${_driscord_contents}/Info.plist" _driscord_info_plist)
foreach(_driscord_plist_key IN ITEMS
        CFBundleExecutable NSMicrophoneUsageDescription
        NSScreenCaptureUsageDescription)
    if(NOT _driscord_info_plist MATCHES "<key>${_driscord_plist_key}</key>")
        message(FATAL_ERROR "Info.plist lacks ${_driscord_plist_key}")
    endif()
endforeach()

foreach(_driscord_forbidden_path IN ITEMS
        Resources/qml/Qt5Compat
        Resources/qml/QtTest
        Resources/qml/QtQuick/Controls/Fusion
        Resources/qml/QtQuick/Controls/Imagine
        Resources/qml/QtQuick/Controls/Material
        Resources/qml/QtQuick/Controls/Universal
        Resources/qml/QtQuick/Controls/iOS
        PlugIns/qmltooling
        Frameworks/Qt5Compat.framework
        Frameworks/QtShaderTools.framework
        Frameworks/QtTest.framework
        Frameworks/QtQuickTest.framework
        Frameworks/QtQuickControls2Fusion.framework
        Frameworks/QtQuickControls2FusionStyleImpl.framework
        Frameworks/QtQuickControls2Imagine.framework
        Frameworks/QtQuickControls2ImagineStyleImpl.framework
        Frameworks/QtQuickControls2Material.framework
        Frameworks/QtQuickControls2MaterialStyleImpl.framework
        Frameworks/QtQuickControls2Universal.framework
        Frameworks/QtQuickControls2UniversalStyleImpl.framework
        Frameworks/QtQuickControls2IOSStyleImpl.framework)
    if(EXISTS "${_driscord_contents}/${_driscord_forbidden_path}")
        message(FATAL_ERROR
            "Forbidden artifact in Runtime component: "
            "Driscord.app/Contents/${_driscord_forbidden_path}")
    endif()
endforeach()

# With LIST_DIRECTORIES, GLOB_RECURSE also returns every directory it walked,
# so the leftovers are picked out by name afterwards.
file(GLOB_RECURSE _driscord_leftovers LIST_DIRECTORIES TRUE
    "${_driscord_contents}/*")
list(FILTER _driscord_leftovers INCLUDE REGEX "(\\.dSYM|/Headers|\\.prl)$")
if(_driscord_leftovers)
    message(FATAL_ERROR
        "Development files leaked into the package: ${_driscord_leftovers}")
endif()

# Every Mach-O must be a thin arm64 image, and every @rpath reference must
# resolve inside Contents/Frameworks -- the only run path the bundle has.
file(GLOB_RECURSE _driscord_macho LIST_DIRECTORIES FALSE
    "${_driscord_contents}/PlugIns/*.dylib"
    "${_driscord_contents}/Resources/qml/*.dylib"
    "${_driscord_contents}/Frameworks/*.dylib"
    "${_driscord_contents}/Frameworks/*.framework/Versions/A/*")
list(FILTER _driscord_macho EXCLUDE REGEX "/Versions/A/Resources/")
list(APPEND _driscord_macho "${_driscord_executable}")
set(_driscord_macho_count 0)
foreach(_driscord_binary IN LISTS _driscord_macho)
    if(IS_SYMLINK "${_driscord_binary}")
        continue()
    endif()
    execute_process(
        COMMAND "${CPACK_DRISCORD_LIPO}" -archs "${_driscord_binary}"
        RESULT_VARIABLE _driscord_result
        OUTPUT_VARIABLE _driscord_archs
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT _driscord_result EQUAL 0 OR NOT _driscord_archs STREQUAL "arm64")
        message(FATAL_ERROR
            "${_driscord_binary} is not a thin arm64 Mach-O "
            "(lipo: '${_driscord_archs}')")
    endif()
    execute_process(
        COMMAND "${CPACK_DRISCORD_OTOOL}" -L "${_driscord_binary}"
        RESULT_VARIABLE _driscord_result
        OUTPUT_VARIABLE _driscord_output
        ERROR_VARIABLE _driscord_error)
    if(NOT _driscord_result EQUAL 0)
        message(FATAL_ERROR
            "otool -L failed for ${_driscord_binary}: ${_driscord_error}")
    endif()
    string(REGEX MATCHALL "\n[\t ]+[^\n]+ \\(compatibility version"
        _driscord_references "${_driscord_output}")
    list(TRANSFORM _driscord_references
        REPLACE "^\n[\t ]+(.+) \\(compatibility version$" "\\1")
    foreach(_driscord_reference IN LISTS _driscord_references)
        if(_driscord_reference MATCHES "^/(System/Library|usr/lib)/")
            continue()
        endif()
        if(_driscord_reference MATCHES "^@rpath/(.+)$")
            set(_driscord_resolved "${_driscord_contents}/Frameworks/${CMAKE_MATCH_1}")
        elseif(_driscord_reference MATCHES "^@loader_path/(.+)$")
            get_filename_component(_driscord_loader_dir "${_driscord_binary}"
                DIRECTORY)
            set(_driscord_resolved "${_driscord_loader_dir}/${CMAKE_MATCH_1}")
        else()
            set(_driscord_resolved "")
        endif()
        if(_driscord_resolved STREQUAL "" OR NOT EXISTS "${_driscord_resolved}")
            message(FATAL_ERROR
                "${_driscord_binary} loads ${_driscord_reference}, which the "
                "bundle does not provide")
        endif()
    endforeach()
    math(EXPR _driscord_macho_count "${_driscord_macho_count} + 1")
endforeach()

message(STATUS
    "Verified staged Driscord.app: ${_driscord_macho_count} thin arm64 Mach-O "
    "images, load commands closed over Contents/Frameworks and the OS")
