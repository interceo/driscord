cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Widgets QuickDialogs2 Core5Compat)
qt_standard_project_setup(REQUIRES 6.5)

if(Qt6Core5Compat_DIR)
    get_filename_component(_QT5COMPAT_CMAKE_ROOT
        "${Qt6Core5Compat_DIR}/.." ABSOLUTE)
    find_package(Qt6qtgraphicaleffectsplugin CONFIG QUIET
        PATHS "${_QT5COMPAT_CMAKE_ROOT}/Qt6Qml/QmlPlugins"
        NO_DEFAULT_PATH)
    find_package(Qt6qtgraphicaleffectsprivate CONFIG QUIET
        PATHS "${_QT5COMPAT_CMAKE_ROOT}/Qt6Qml/QmlPlugins"
        NO_DEFAULT_PATH)
endif()

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
        src/app/FrameProvider.h
        src/app/FrameProvider.cpp
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

target_include_directories(driscord_client PRIVATE src)

# The core library has been built with these all along; the client was the one
# place in the tree compiling with no warnings at all.
if(MSVC)
    target_compile_options(driscord_client PRIVATE /W4)
else()
    target_compile_options(driscord_client PRIVATE -Wall -Wextra)
endif()

target_link_libraries(driscord_client PRIVATE
    driscord_core
    Qt6::Quick
    Qt6::Network
    Qt6::Widgets
    Qt6::QuickDialogs2
    Qt6::Core5Compat
)

if(UNIX AND NOT APPLE AND TARGET datachannel)
    # libdatachannel is deliberately a DSO isolation boundary next to Google
    # WebRTC. Make a build-tree client directory self-contained instead of
    # embedding an absolute path to FetchContent's temporary build directory.
    set_target_properties(driscord_client PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN")
    add_custom_command(TARGET driscord_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:datachannel>
            $<TARGET_FILE_DIR:driscord_client>/$<TARGET_FILE_NAME:datachannel>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:datachannel>
            $<TARGET_FILE_DIR:driscord_client>/$<TARGET_SONAME_FILE_NAME:datachannel>
        VERBATIM)
endif()

if(WIN32)
    set_target_properties(driscord_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
