# Derive the user-visible version from the repository state. Exact semver tags
# are release versions; other commits keep a numeric CMake project version and
# add deterministic development metadata for diagnostics and package names.
set(DRISCORD_VERSION_CORE "0.0.0")
set(DRISCORD_VERSION_DISPLAY "0.0.0+unknown")
set(DRISCORD_VERSION_IS_RELEASE OFF)

find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe
            --tags --match "v[0-9]*.[0-9]*.[0-9]*" --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        RESULT_VARIABLE _driscord_git_result
        OUTPUT_VARIABLE _driscord_git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(_driscord_git_result EQUAL 0)
        if(_driscord_git_describe MATCHES
            "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
            set(DRISCORD_VERSION_CORE "${CMAKE_MATCH_1}")
            set(DRISCORD_VERSION_DISPLAY "${CMAKE_MATCH_1}")
            set(DRISCORD_VERSION_IS_RELEASE ON)
        elseif(_driscord_git_describe MATCHES
            "^v([0-9]+\\.[0-9]+\\.[0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
            set(DRISCORD_VERSION_CORE "${CMAKE_MATCH_1}")
            set(DRISCORD_VERSION_DISPLAY
                "${CMAKE_MATCH_1}-dev.${CMAKE_MATCH_2}+g${CMAKE_MATCH_3}")
            if(CMAKE_MATCH_4)
                string(APPEND DRISCORD_VERSION_DISPLAY ".dirty")
            endif()
        elseif(_driscord_git_describe MATCHES
            "^([0-9a-f]+)(-dirty)?$")
            set(DRISCORD_VERSION_DISPLAY "0.0.0+g${CMAKE_MATCH_1}")
            if(CMAKE_MATCH_2)
                string(APPEND DRISCORD_VERSION_DISPLAY ".dirty")
            endif()
        endif()
    endif()
endif()

unset(_driscord_git_describe)
unset(_driscord_git_result)
