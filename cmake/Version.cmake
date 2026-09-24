set(DRISCORD_VERSION_CORE "0.0.0")
set(DRISCORD_VERSION_DISPLAY "0.0.0+unknown")
set(DRISCORD_VERSION_IS_RELEASE OFF)

find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../.git")
    # Client releases are tagged client-vX.Y.Z (server releases server-vX.Y.Z
    # version nothing in this tree); bare vX.Y.Z tags predate the split and
    # still anchor dev versions until the first client release.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe
            --tags --match "client-v[0-9]*.[0-9]*.[0-9]*"
            --match "v[0-9]*.[0-9]*.[0-9]*" --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        RESULT_VARIABLE _driscord_git_result
        OUTPUT_VARIABLE _driscord_git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(_driscord_git_result EQUAL 0)
        set(_driscord_git_dirty "")
        if(_driscord_git_describe MATCHES "-dirty$")
            set(_driscord_git_dirty ".dirty")
            string(REGEX REPLACE "-dirty$" "" _driscord_git_describe
                "${_driscord_git_describe}")
        endif()
        # A prerelease tag (client-v1.2.0-rc.1) keeps its suffix in the display
        # version but is not a release build: the updater and the update
        # channel compare plain X.Y.Z only.
        if(_driscord_git_describe MATCHES
            "^(client-)?v([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.]+)?$")
            set(DRISCORD_VERSION_CORE "${CMAKE_MATCH_2}")
            set(DRISCORD_VERSION_DISPLAY
                "${CMAKE_MATCH_2}${CMAKE_MATCH_3}${_driscord_git_dirty}")
            if(NOT CMAKE_MATCH_3 AND NOT _driscord_git_dirty)
                set(DRISCORD_VERSION_IS_RELEASE ON)
            endif()
        elseif(_driscord_git_describe MATCHES
            "^(client-)?v([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.]+)?-([0-9]+)-g([0-9a-f]+)$")
            set(DRISCORD_VERSION_CORE "${CMAKE_MATCH_2}")
            set(DRISCORD_VERSION_DISPLAY
                "${CMAKE_MATCH_2}${CMAKE_MATCH_3}-dev.${CMAKE_MATCH_4}+g${CMAKE_MATCH_5}${_driscord_git_dirty}")
        elseif(_driscord_git_describe MATCHES "^([0-9a-f]+)$")
            set(DRISCORD_VERSION_DISPLAY
                "0.0.0+g${CMAKE_MATCH_1}${_driscord_git_dirty}")
        endif()
        unset(_driscord_git_dirty)
    endif()
endif()

unset(_driscord_git_describe)
unset(_driscord_git_result)
