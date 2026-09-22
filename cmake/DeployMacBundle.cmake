# Completes the staged Driscord.app: copies every Qt framework and dylib the
# executable, its plugins and its QML plugins reach, then thins each Mach-O
# to arm64.
#
# Nothing here rewrites a load command. Qt's binaries arrive signed and the
# executable carries the linker's ad-hoc signature, which is what lets arm64
# macOS run them at all; install_name_tool would invalidate those signatures
# and leave no way to re-sign on this host. The layout is chosen so that the
# references already resolve: the executable's LC_RPATH is
# @executable_path/../Frameworks, and dyld searches the main executable's run
# paths for every image it loads, including dlopen()ed plugins. Thinning keeps
# the arm64 slice byte for byte, and each slice carries its own signature.
#
# Inputs: DRISCORD_BUNDLE, DRISCORD_QT_LIB_DIR, DRISCORD_OTOOL, DRISCORD_LIPO.

cmake_policy(VERSION 3.25)

foreach(_driscord_input DRISCORD_BUNDLE DRISCORD_QT_LIB_DIR DRISCORD_OTOOL
        DRISCORD_LIPO)
    if("${${_driscord_input}}" STREQUAL "")
        message(FATAL_ERROR "DeployMacBundle.cmake needs ${_driscord_input}")
    endif()
endforeach()

set(_driscord_contents "${DRISCORD_BUNDLE}/Contents")
set(_driscord_frameworks "${_driscord_contents}/Frameworks")
file(MAKE_DIRECTORY "${_driscord_frameworks}")

function(_driscord_macho_references _driscord_binary _driscord_out)
    execute_process(
        COMMAND "${DRISCORD_OTOOL}" -arch arm64 -L "${_driscord_binary}"
        RESULT_VARIABLE _driscord_result
        OUTPUT_VARIABLE _driscord_output
        ERROR_VARIABLE _driscord_error)
    if(NOT _driscord_result EQUAL 0)
        message(FATAL_ERROR
            "otool -L failed for ${_driscord_binary}: ${_driscord_error}")
    endif()
    # The first line names the file itself; every following line is one
    # load command, "<path> (compatibility version ..., current version ...)".
    string(REGEX MATCHALL "\n[\t ]+[^\n]+ \\(compatibility version"
        _driscord_lines "${_driscord_output}")
    list(TRANSFORM _driscord_lines
        REPLACE "^\n[\t ]+(.+) \\(compatibility version$" "\\1")
    set(${_driscord_out} "${_driscord_lines}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _driscord_pending LIST_DIRECTORIES FALSE
    "${_driscord_contents}/PlugIns/*.dylib"
    "${_driscord_contents}/Resources/qml/*.dylib")
list(PREPEND _driscord_pending "${_driscord_contents}/MacOS/driscord_client")
set(_driscord_macho "${_driscord_pending}")

while(_driscord_pending)
    list(POP_FRONT _driscord_pending _driscord_binary)
    _driscord_macho_references("${_driscord_binary}" _driscord_references)
    get_filename_component(_driscord_binary_dir "${_driscord_binary}" DIRECTORY)
    foreach(_driscord_reference IN LISTS _driscord_references)
        if(_driscord_reference MATCHES "^/(System/Library|usr/lib)/")
            continue()
        endif()
        # FFmpeg's dylibs name their siblings through @loader_path; they all
        # land flat in Frameworks, where that spelling and @rpath mean one place.
        if(_driscord_binary_dir STREQUAL _driscord_frameworks)
            string(REGEX REPLACE "^@loader_path/([^/]+\\.dylib)$" "@rpath/\\1"
                _driscord_reference "${_driscord_reference}")
        endif()
        if(_driscord_reference MATCHES
                "^@rpath/(([^/]+)\\.framework/Versions/[^/]+/[^/]+)$")
            set(_driscord_relative "${CMAKE_MATCH_1}")
            set(_driscord_framework "${CMAKE_MATCH_2}.framework")
            set(_driscord_target "${_driscord_frameworks}/${_driscord_relative}")
            if(EXISTS "${_driscord_target}")
                continue()
            endif()
            if(NOT EXISTS "${DRISCORD_QT_LIB_DIR}/${_driscord_relative}")
                message(FATAL_ERROR
                    "${_driscord_binary} needs ${_driscord_reference}, which "
                    "is not in the Qt kit at ${DRISCORD_QT_LIB_DIR}")
            endif()
            file(COPY "${DRISCORD_QT_LIB_DIR}/${_driscord_framework}"
                DESTINATION "${_driscord_frameworks}"
                PATTERN "Headers" EXCLUDE
                PATTERN "*.prl" EXCLUDE
                PATTERN "*.dSYM" EXCLUDE)
        elseif(_driscord_reference MATCHES "^@rpath/([^/]+\\.dylib)$")
            set(_driscord_relative "${CMAKE_MATCH_1}")
            set(_driscord_target "${_driscord_frameworks}/${_driscord_relative}")
            if(EXISTS "${_driscord_target}")
                continue()
            endif()
            if(NOT EXISTS "${DRISCORD_QT_LIB_DIR}/${_driscord_relative}")
                message(FATAL_ERROR
                    "${_driscord_binary} needs ${_driscord_reference}, which "
                    "is not in the Qt kit at ${DRISCORD_QT_LIB_DIR}")
            endif()
            # The kit names these through a symlink chain; the reference is
            # the only name dyld asks for, so the real file ships under it.
            file(REAL_PATH "${DRISCORD_QT_LIB_DIR}/${_driscord_relative}"
                _driscord_real)
            file(COPY_FILE "${_driscord_real}" "${_driscord_target}")
        else()
            message(FATAL_ERROR
                "${_driscord_binary} loads ${_driscord_reference}, which is "
                "neither an operating-system library nor an @rpath reference "
                "the bundle can satisfy")
        endif()
        list(APPEND _driscord_pending "${_driscord_target}")
        list(APPEND _driscord_macho "${_driscord_target}")
    endforeach()
endwhile()

# The Qt kit is universal. Only the arm64 slice can ever run here: the WebRTC
# archive and the executable are arm64-only.
foreach(_driscord_binary IN LISTS _driscord_macho)
    execute_process(
        COMMAND "${DRISCORD_LIPO}" -archs "${_driscord_binary}"
        RESULT_VARIABLE _driscord_result
        OUTPUT_VARIABLE _driscord_archs
        ERROR_VARIABLE _driscord_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _driscord_result EQUAL 0)
        message(FATAL_ERROR
            "lipo -archs failed for ${_driscord_binary}: ${_driscord_error}")
    endif()
    separate_arguments(_driscord_archs UNIX_COMMAND "${_driscord_archs}")
    if(NOT "arm64" IN_LIST _driscord_archs)
        message(FATAL_ERROR "${_driscord_binary} has no arm64 slice")
    endif()
    if(_driscord_archs STREQUAL "arm64")
        continue()
    endif()
    execute_process(
        COMMAND "${DRISCORD_LIPO}" "${_driscord_binary}" -thin arm64
            -output "${_driscord_binary}.thin"
        RESULT_VARIABLE _driscord_result
        ERROR_VARIABLE _driscord_error)
    if(NOT _driscord_result EQUAL 0)
        message(FATAL_ERROR
            "lipo -thin failed for ${_driscord_binary}: ${_driscord_error}")
    endif()
    file(RENAME "${_driscord_binary}.thin" "${_driscord_binary}")
endforeach()
