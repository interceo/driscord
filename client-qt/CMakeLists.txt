cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Widgets QuickDialogs2)
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
    src/api/AudioSettingsStore.cpp
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
        qml/components/VideoTile.qml
        qml/dialogs/SettingsDialog.qml
        qml/dialogs/ShareDialog.qml
        qml/dialogs/CreateServerDialog.qml
        qml/dialogs/CreateChannelDialog.qml
        qml/dialogs/AvatarCropDialog.qml
        qml/dialogs/JoinByInviteDialog.qml
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

target_link_libraries(driscord_client PRIVATE
    driscord_core
    Qt6::Quick
    Qt6::Network
    Qt6::Widgets
    Qt6::QuickDialogs2
)

if(WIN32)
    set_target_properties(driscord_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
