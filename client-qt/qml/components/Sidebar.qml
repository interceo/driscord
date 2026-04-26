import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#2b2d31"

    signal settingsRequested
    signal shareRequested
    signal voiceStatsRequested

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Server name header
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "#2b2d31"
            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 16 }
                text: {
                    var s = appState.servers
                    for (var i = 0; i < s.length; i++)
                        if (s[i].id === appState.selectedServerId) return s[i].name
                    return "Driscord"
                }
                color: "white"; font { pixelSize: 15; bold: true }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#1e1f22" }

        // Channel list
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 2

                Item { implicitHeight: 8 }

                // Voice channels
                Text {
                    text: "VOICE CHANNELS"
                    color: "#72767d"; font.pixelSize: 11
                    leftPadding: 16; topPadding: 4
                }

                Repeater {
                    model: appState.channels.filter(ch => ch.kind === "voice")
                    delegate: ColumnLayout {
                        width: parent.width
                        spacing: 0

                        readonly property var channel: modelData
                        readonly property bool isJoined:
                            appState.selectedChannelId === channel.id
                            && appState.connectionState !== "disconnected"

                        // Channel row
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            radius: 4
                            color: appState.selectedChannelId === channel.id ? "#3c3f45" : "transparent"
                            RowLayout {
                                anchors {
                                    left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                    leftMargin: 16; rightMargin: 8
                                }
                                spacing: 6
                                IconBox { source: "qrc:/icons/volume.svg"; size: 16; color: "#b9bbbe" }
                                Text {
                                    text: channel.name; color: "#dcddde"; font.pixelSize: 14
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: appState.joinVoiceChannel(channel.id)
                            }
                        }

                        // Local user — shown when this channel is the joined one
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 28
                            spacing: 6
                            visible: isJoined
                            AvatarBox {
                                size: 18
                                displayName: authManager.username
                                avatarUrl: appState.userProfile.avatarUrl ?? ""
                            }
                            Text {
                                text: (appState.userProfile.displayName && appState.userProfile.displayName.length > 0)
                                      ? appState.userProfile.displayName
                                      : authManager.username
                                color: "#dcddde"; font.pixelSize: 13
                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                            IconBox {
                                visible: appState.muted
                                source: "qrc:/icons/mic-off.svg"; color: "#ed4245"; size: 14
                            }
                            IconBox {
                                visible: appState.deafened
                                source: "qrc:/icons/headphones-off.svg"; color: "#ed4245"; size: 14
                            }
                        }

                        // Remote peers — shown when this channel is the joined one
                        Repeater {
                            model: isJoined ? appState.peers : []
                            delegate: RowLayout {
                                readonly property string label:
                                    (modelData.displayName && modelData.displayName.length > 0)
                                        ? modelData.displayName
                                        : (modelData.username && modelData.username.length > 0
                                           ? modelData.username : modelData.id)
                                Layout.fillWidth: true
                                Layout.leftMargin: 28
                                spacing: 6
                                AvatarBox {
                                    size: 18
                                    displayName: label
                                    avatarUrl: modelData.avatarUrl ?? ""
                                }
                                Text {
                                    text: label
                                    color: "#dcddde"; font.pixelSize: 13
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                // Add channel button
                Text {
                    text: "+ Add Channel"; color: "#72767d"; font.pixelSize: 13
                    leftPadding: 16; topPadding: 4
                    MouseArea { anchors.fill: parent; onClicked: createChannelDialog.open(); cursorShape: Qt.PointingHandCursor }
                }
            }
        }

        // Voice connected status banner — shows server/channel name + ping popup trigger
        Rectangle {
            id: voiceStatusBanner
            Layout.fillWidth: true
            height: appState.connectionState !== "disconnected" ? 52 : 0
            color: "#1a1b1e"
            visible: appState.connectionState !== "disconnected"
            clip: true

            readonly property bool isConnecting: appState.connectionState === "connecting"
            readonly property color statusColor: isConnecting ? "#faa61a" : "#3ba55d"

            RowLayout {
                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                spacing: 8

                // Signal/wifi icon — clickable to open stats popup (when connected)
                Rectangle {
                    id: signalIcon
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    color: "transparent"; radius: 4

                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 18; height: 18
                        running: voiceStatusBanner.isConnecting
                        visible: running
                    }
                    IconBox {
                        anchors.centerIn: parent
                        source: "qrc:/icons/signal.svg"
                        color: voiceStatusBanner.statusColor
                        size: 18
                        visible: !voiceStatusBanner.isConnecting
                    }
                    MouseArea {
                        id: signalMouse
                        anchors.fill: parent
                        cursorShape: appState.connected ? Qt.PointingHandCursor : Qt.ArrowCursor
                        hoverEnabled: true
                        onEntered: if (appState.connected) signalIcon.color = "#2c2d31"
                        onExited:  signalIcon.color = "transparent"
                        onClicked: if (appState.connected) root.voiceStatsRequested()
                    }
                    ToolTip.visible: signalMouse.containsMouse && appState.connected
                    ToolTip.text: qsTr("Voice connection details")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: voiceStatusBanner.isConnecting ? qsTr("Connecting…") : qsTr("Voice connected")
                        color: voiceStatusBanner.statusColor
                        font { pixelSize: 13; bold: true }
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                    Text {
                        text: {
                            var sName = "", cName = ""
                            for (var i = 0; i < appState.servers.length; i++)
                                if (appState.servers[i].id === appState.selectedServerId) sName = appState.servers[i].name
                            for (var j = 0; j < appState.channels.length; j++)
                                if (appState.channels[j].id === appState.selectedChannelId) cName = appState.channels[j].name
                            if (sName && cName) return sName + " / " + cName
                            return cName || sName
                        }
                        color: "#b9bbbe"; font.pixelSize: 11
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }

                IconButton {
                    icon.source: "qrc:/icons/x.svg"
                    iconSize: 16
                    onClicked: appState.leaveVoiceChannel()
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Disconnect")
                }
            }
        }

        // Voice bar at bottom
        Rectangle {
            Layout.fillWidth: true
            height: appState.connected ? 52 : 0
            color: "#232428"
            visible: appState.connected
            clip: true

            RowLayout {
                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                spacing: 4

                AvatarBox {
                    size: 28
                    displayName: appState.userProfile.displayName || authManager.username
                    avatarUrl: appState.userProfile.avatarUrl ?? ""
                }

                Text {
                    text: appState.userProfile.displayName || authManager.username; color: "white"
                    font.pixelSize: 13; Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                // Mute
                IconButton {
                    icon.source: appState.muted ? "qrc:/icons/mic-off.svg" : "qrc:/icons/mic.svg"
                    iconColor: appState.muted ? "#ed4245" : "#dcddde"
                    onClicked: appState.setMuted(!appState.muted)
                    ToolTip.visible: hovered; ToolTip.text: appState.muted ? "Unmute" : "Mute"
                }

                // Deafen
                IconButton {
                    icon.source: appState.deafened ? "qrc:/icons/headphones-off.svg" : "qrc:/icons/headphones.svg"
                    iconColor: appState.deafened ? "#ed4245" : "#dcddde"
                    onClicked: appState.setDeafened(!appState.deafened)
                    ToolTip.visible: hovered; ToolTip.text: appState.deafened ? "Undeafen" : "Deafen"
                }

                // Share
                IconButton {
                    icon.source: appState.sharing ? "qrc:/icons/monitor-off.svg" : "qrc:/icons/monitor.svg"
                    iconColor: appState.sharing ? "#ed4245" : "#dcddde"
                    onClicked: appState.sharing ? appState.stopSharing() : root.shareRequested()
                    ToolTip.visible: hovered; ToolTip.text: appState.sharing ? "Stop sharing" : "Share screen"
                }

                // Settings
                IconButton {
                    icon.source: "qrc:/icons/settings.svg"
                    onClicked: root.settingsRequested()
                    ToolTip.visible: hovered; ToolTip.text: "Settings"
                }
            }
        }

        // Disconnect / user bar (not connected)
        Rectangle {
            Layout.fillWidth: true
            height: 52
            color: "#232428"
            visible: !appState.connected

            RowLayout {
                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                spacing: 8
                AvatarBox {
                    size: 28
                    displayName: appState.userProfile.displayName || authManager.username
                    avatarUrl: appState.userProfile.avatarUrl ?? ""
                }
                Text {
                    text: appState.userProfile.displayName || authManager.username; color: "white"; font.pixelSize: 13; Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                IconButton {
                    icon.source: "qrc:/icons/settings.svg"
                    onClicked: root.settingsRequested()
                }
            }
        }
    }
}
