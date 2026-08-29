cmake_minimum_required(VERSION 3.20)

include(GNUInstallDirs)
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network Widgets QuickDialogs2 Multimedia)
qt_standard_project_setup(REQUIRES 6.5)

set(CLIENT_SOURCES
    src/main.cpp
    src/app/AppState.cpp
    src/app/AppConfig.cpp
    src/app/AvatarTintProvider.cpp
    src/app/DriscordBridge.cpp
    src/api/ApiClient.cpp
    src/api/AuthManager.cpp
    src/api/SessionStore.cpp
    src/api/ServerRepository.cpp
    src/api/UserRepository.cpp
    src/update/UpdateManifest.cpp
    src/update/Minisign.cpp
    src/update/InstallLayout.cpp
    src/update/InstallSwap.cpp
    src/update/UpdateManager.cpp
)

qt_add_executable(driscord_client ${CLIENT_SOURCES})

qt_add_qml_module(driscord_client
    URI driscord
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/screens/LoginScreen.qml
        qml/screens/MainScreen.qml
        qml/components/ServerColumn.qml
        qml/components/Sidebar.qml
        qml/components/ContentPanel.qml
        qml/components/AvatarBox.qml
        qml/components/IconBox.qml
        qml/components/IconButton.qml
        qml/components/StreamStatsOverlay.qml
        qml/components/VideoTile.qml
        qml/components/DiscordButton.qml
        qml/components/DiscordComboBox.qml
        qml/components/DiscordDialog.qml
        qml/components/DiscordTextField.qml
        qml/dialogs/SettingsDialog.qml
        qml/dialogs/ShareDialog.qml
        qml/dialogs/CreateServerDialog.qml
        qml/dialogs/CreateChannelDialog.qml
        qml/dialogs/AvatarCropDialog.qml
        qml/dialogs/JoinByInviteDialog.qml
        qml/dialogs/InviteUsersDialog.qml
        qml/dialogs/VoiceStatsDialog.qml
    SOURCES
        src/app/ThumbnailProvider.h
        src/app/ThumbnailProvider.cpp
)

qt_add_resources(driscord_client "icons"
    PREFIX "/icons"
    BASE resources/icons
    FILES
        resources/icons/volume.svg
        resources/icons/signal.svg
        resources/icons/mic.svg
        resources/icons/mic-off.svg
        resources/icons/headphones.svg
        resources/icons/headphones-off.svg
        resources/icons/monitor.svg
        resources/icons/monitor-off.svg
        resources/icons/settings.svg
        resources/icons/x.svg
        resources/icons/plus.svg
)

target_include_directories(driscord_client PRIVATE
    src
    "${CMAKE_BINARY_DIR}/generated")

if(MSVC)
    target_compile_options(driscord_client PRIVATE /W4)
else()
    target_compile_options(driscord_client PRIVATE -Wall -Wextra)
endif()

target_link_libraries(driscord_client PRIVATE
    driscord_core
    driscord::boringssl_headers
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
    Qt6::QuickDialogs2
    Qt6::Multimedia
)

if(UNIX AND NOT APPLE AND TARGET datachannel)
    set_target_properties(driscord_client PROPERTIES
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:Qt6::Core>"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    add_custom_command(TARGET driscord_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:datachannel>
            $<TARGET_FILE_DIR:driscord_client>/$<TARGET_FILE_NAME:datachannel>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:datachannel>
            $<TARGET_FILE_DIR:driscord_client>/$<TARGET_SONAME_FILE_NAME:datachannel>
        VERBATIM)
endif()

if(UNIX AND NOT APPLE AND DRISCORD_ENABLE_PACKAGING)
    add_executable(driscord_launcher packaging/launcher.c)
    set_target_properties(driscord_launcher PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        LINKER_LANGUAGE C
        OUTPUT_NAME driscord
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/launcher"
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH "")
    target_compile_options(driscord_launcher PRIVATE -Wall -Wextra -Wpedantic)

    install(TARGETS driscord_client
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime)

    install(TARGETS driscord_launcher
        RUNTIME
        DESTINATION .
        COMPONENT Runtime)

    install(PROGRAMS $<TARGET_FILE:driscord_launcher>
        DESTINATION . RENAME AppRun COMPONENT Runtime)
    install(FILES packaging/appimage/driscord.desktop
        DESTINATION . COMPONENT Runtime)
    install(FILES packaging/appimage/driscord.svg
        DESTINATION . COMPONENT Runtime)
    install(FILES packaging/appimage/driscord.svg
        DESTINATION . RENAME .DirIcon COMPONENT Runtime)

    install(FILES packaging/README.txt
        DESTINATION .
        COMPONENT Runtime)

    install(TARGETS datachannel
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT Runtime
        NAMELINK_COMPONENT Runtime)

    foreach(_driscord_headless_plugin IN ITEMS
            Qt6::QOffscreenIntegrationPlugin
            Qt6::QMinimalIntegrationPlugin)
        if(NOT TARGET ${_driscord_headless_plugin})
            message(FATAL_ERROR
                "Required Qt platform plugin target is unavailable: "
                "${_driscord_headless_plugin}")
        endif()
        install(IMPORTED_RUNTIME_ARTIFACTS ${_driscord_headless_plugin}
            LIBRARY DESTINATION plugins/platforms
            COMPONENT Runtime)
    endforeach()
    unset(_driscord_headless_plugin)

    qt_generate_deploy_qml_app_script(
        TARGET driscord_client
        NO_TRANSLATIONS
        OUTPUT_SCRIPT driscord_deploy_script)
    install(SCRIPT ${driscord_deploy_script}
        COMPONENT Runtime)

    install(CODE [[
        file(REMOVE
            "${CMAKE_INSTALL_PREFIX}/plugins/multimedia/libgstreamermediaplugin.so")
    ]] COMPONENT Runtime)

    install(CODE [[
        foreach(_driscord_pruned_dir IN ITEMS
                plugins/qmltooling
                plugins/egldeviceintegrations
                qml/QtTest
                qml/QtQuick/Controls/Fusion
                qml/QtQuick/Controls/Imagine
                qml/QtQuick/Controls/Material
                qml/QtQuick/Controls/Universal)
            file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/${_driscord_pruned_dir}")
        endforeach()
        foreach(_driscord_pruned_lib IN ITEMS
                libQt6Test
                libQt6QuickTest
                libQt6EglFSDeviceIntegration
                libQt6EglFsKmsSupport
                libQt6EglFsKmsGbmSupport
                libQt6QuickControls2Fusion
                libQt6QuickControls2FusionStyleImpl
                libQt6QuickControls2Imagine
                libQt6QuickControls2ImagineStyleImpl
                libQt6QuickControls2Material
                libQt6QuickControls2MaterialStyleImpl
                libQt6QuickControls2Universal
                libQt6QuickControls2UniversalStyleImpl)
            file(GLOB _driscord_pruned_files
                "${CMAKE_INSTALL_PREFIX}/lib/${_driscord_pruned_lib}.so*")
            if(_driscord_pruned_files)
                file(REMOVE ${_driscord_pruned_files})
            endif()
        endforeach()
    ]] COMPONENT Runtime)
endif()

if(WIN32)
    set_target_properties(driscord_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(WIN32 AND DRISCORD_ENABLE_PACKAGING)
    find_package(Qt6 REQUIRED COMPONENTS Svg)

    if("$ENV{DRISCORD_MSVC_REDIST_DIR}" STREQUAL "")
        message(FATAL_ERROR
            "Windows packaging needs DRISCORD_MSVC_REDIST_DIR: a directory "
            "with the Microsoft.VC143.CRT redistributable DLLs "
            "(msvcp140*.dll, vcruntime140*.dll) for the /MD Qt libraries.")
    endif()
    file(TO_CMAKE_PATH "$ENV{DRISCORD_MSVC_REDIST_DIR}" _driscord_redist_dir)
    if(NOT EXISTS "${_driscord_redist_dir}/msvcp140.dll")
        message(FATAL_ERROR
            "DRISCORD_MSVC_REDIST_DIR=${_driscord_redist_dir} does not "
            "contain msvcp140.dll")
    endif()

    foreach(_driscord_qt_var QT6_INSTALL_PREFIX QT6_INSTALL_QML QT6_INSTALL_BINS)
        if("${${_driscord_qt_var}}" STREQUAL "")
            message(FATAL_ERROR "${_driscord_qt_var} is not provided by Qt6")
        endif()
    endforeach()
    set(_driscord_qt_qml_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    set(_driscord_qt_bin_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")

    install(TARGETS driscord_client
        RUNTIME DESTINATION .
        COMPONENT Runtime)

    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/packaging/qt.conf"
        CONTENT "[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\n"
        NEWLINE_STYLE UNIX)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/qt.conf"
        DESTINATION . COMPONENT Runtime)

    set(_driscord_win_plugins
        Qt6::QWindowsIntegrationPlugin
        Qt6::QModernWindowsStylePlugin
        Qt6::QSvgIconPlugin
        Qt6::QSvgPlugin
        Qt6::QJpegPlugin
        Qt6::QICOPlugin
        Qt6::QGifPlugin
        Qt6::QSchannelBackendPlugin
        Qt6::QFFmpegMediaPlugin
        Qt6::QWindowsMediaPlugin)
    foreach(_driscord_win_plugin IN LISTS _driscord_win_plugins)
        if(NOT TARGET ${_driscord_win_plugin})
            message(FATAL_ERROR
                "Required Qt plugin target is unavailable: "
                "${_driscord_win_plugin}")
        endif()
        get_target_property(_driscord_win_plugin_type
            ${_driscord_win_plugin} QT_PLUGIN_TYPE)
        if(NOT _driscord_win_plugin_type)
            message(FATAL_ERROR
                "${_driscord_win_plugin} does not declare QT_PLUGIN_TYPE")
        endif()
        install(IMPORTED_RUNTIME_ARTIFACTS ${_driscord_win_plugin}
            RUNTIME DESTINATION "plugins/${_driscord_win_plugin_type}"
            LIBRARY DESTINATION "plugins/${_driscord_win_plugin_type}"
            COMPONENT Runtime)
    endforeach()
    unset(_driscord_win_plugin)
    unset(_driscord_win_plugin_type)

    set(_driscord_win_qml_modules
        Qt/labs/folderlistmodel
        QtMultimedia
        QtQml
        QtQml/Base
        QtQml/Models
        QtQml/WorkerScript
        QtQuick
        QtQuick/Controls
        QtQuick/Controls/Basic
        QtQuick/Controls/Basic/impl
        QtQuick/Controls/Windows
        QtQuick/Controls/impl
        QtQuick/Dialogs
        QtQuick/Dialogs/quickimpl
        QtQuick/Effects
        QtQuick/Layouts
        QtQuick/NativeStyle
        QtQuick/Shapes
        QtQuick/Templates
        QtQuick/Window)
    foreach(_driscord_win_qml_module IN LISTS _driscord_win_qml_modules)
        if(NOT EXISTS "${_driscord_qt_qml_dir}/${_driscord_win_qml_module}/qmldir")
            message(FATAL_ERROR
                "QML module is missing from the Windows Qt kit: "
                "${_driscord_win_qml_module}")
        endif()
        install(DIRECTORY "${_driscord_qt_qml_dir}/${_driscord_win_qml_module}/"
            DESTINATION "qml/${_driscord_win_qml_module}"
            COMPONENT Runtime
            PATTERN "*.pdb" EXCLUDE
            PATTERN "*d.dll" EXCLUDE)
    endforeach()
    unset(_driscord_win_qml_module)

    install(CODE "set(DRISCORD_QT_BIN_DIR \"${_driscord_qt_bin_dir}\")"
        COMPONENT Runtime)
    install(CODE "set(DRISCORD_MSVC_REDIST_DIR \"${_driscord_redist_dir}\")"
        COMPONENT Runtime)
    install(CODE "set(CMAKE_OBJDUMP \"${CMAKE_OBJDUMP}\")" COMPONENT Runtime)
    install(CODE
        "include(\"${CMAKE_SOURCE_DIR}/cmake/WindowsSystemDlls.cmake\")"
        COMPONENT Runtime)
    install(CODE [[
        # Install scripts run under CMake's minimum policy floor, where
        # IN_LIST (CMP0057) is not an if() operator yet.
        cmake_policy(VERSION 3.25)
        function(_driscord_pe_imports _driscord_binary _driscord_out)
            execute_process(
                COMMAND "${CMAKE_OBJDUMP}" -p "${_driscord_binary}"
                RESULT_VARIABLE _driscord_objdump_result
                OUTPUT_VARIABLE _driscord_objdump_output
                ERROR_VARIABLE _driscord_objdump_error)
            if(NOT _driscord_objdump_result EQUAL 0)
                message(FATAL_ERROR
                    "objdump failed for ${_driscord_binary}: "
                    "${_driscord_objdump_error}")
            endif()
            string(REGEX MATCHALL "DLL Name: [^\n]+" _driscord_imports
                "${_driscord_objdump_output}")
            list(TRANSFORM _driscord_imports
                REPLACE "^DLL Name: *" "")
            list(TRANSFORM _driscord_imports STRIP)
            list(TRANSFORM _driscord_imports TOLOWER)
            set(${_driscord_out} "${_driscord_imports}" PARENT_SCOPE)
        endfunction()

        # PE import references are case-insensitive; index the search
        # directories by lowercased file name once.
        foreach(_driscord_search_dir IN ITEMS
                "${DRISCORD_QT_BIN_DIR}" "${DRISCORD_MSVC_REDIST_DIR}")
            file(GLOB _driscord_search_files LIST_DIRECTORIES FALSE
                "${_driscord_search_dir}/*.dll")
            foreach(_driscord_search_file IN LISTS _driscord_search_files)
                get_filename_component(_driscord_search_name
                    "${_driscord_search_file}" NAME)
                string(TOLOWER "${_driscord_search_name}" _driscord_search_name)
                if(NOT DEFINED "_driscord_provider_${_driscord_search_name}")
                    set(_driscord_provider_${_driscord_search_name}
                        "${_driscord_search_file}")
                endif()
            endforeach()
        endforeach()

        file(GLOB_RECURSE _driscord_pending LIST_DIRECTORIES FALSE
            "${CMAKE_INSTALL_PREFIX}/plugins/*.dll"
            "${CMAKE_INSTALL_PREFIX}/qml/*.dll")
        list(APPEND _driscord_pending
            "${CMAKE_INSTALL_PREFIX}/driscord_client.exe")
        set(_driscord_shipped "")
        file(GLOB _driscord_root_binaries LIST_DIRECTORIES FALSE
            "${CMAKE_INSTALL_PREFIX}/*.dll")
        foreach(_driscord_binary IN LISTS _driscord_pending
                _driscord_root_binaries)
            get_filename_component(_driscord_name "${_driscord_binary}" NAME)
            string(TOLOWER "${_driscord_name}" _driscord_name)
            list(APPEND _driscord_shipped "${_driscord_name}")
        endforeach()

        while(_driscord_pending)
            list(POP_FRONT _driscord_pending _driscord_binary)
            _driscord_pe_imports("${_driscord_binary}" _driscord_imports)
            foreach(_driscord_import IN LISTS _driscord_imports)
                if(_driscord_import IN_LIST _driscord_shipped
                        OR _driscord_import MATCHES
                            "${DRISCORD_WINDOWS_SYSTEM_DLL_REGEX}")
                    continue()
                endif()
                if(NOT DEFINED "_driscord_provider_${_driscord_import}")
                    message(FATAL_ERROR
                        "${_driscord_binary} imports ${_driscord_import}, "
                        "which is neither packaged, in the Qt kit, in the CRT "
                        "redist, nor an operating-system DLL")
                endif()
                file(INSTALL "${_driscord_provider_${_driscord_import}}"
                    DESTINATION "${CMAKE_INSTALL_PREFIX}")
                list(APPEND _driscord_shipped "${_driscord_import}")
                get_filename_component(_driscord_provider_name
                    "${_driscord_provider_${_driscord_import}}" NAME)
                list(APPEND _driscord_pending
                    "${CMAKE_INSTALL_PREFIX}/${_driscord_provider_name}")
            endforeach()
        endwhile()
    ]] COMPONENT Runtime)
    unset(_driscord_win_qml_modules)
    unset(_driscord_win_plugins)
    unset(_driscord_qt_qml_dir)
    unset(_driscord_qt_bin_dir)
    unset(_driscord_redist_dir)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
