import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Dialog {
    id: root
    title: "Settings"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 680; height: 520
    background: Rectangle { color: "#313338" }
    header: Item {
        height: 42
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 16 }
            text: "Settings"; color: "white"; font { pixelSize: 15; bold: true }
        }
    }

    AvatarCropDialog { id: avatarCropDlg }

    RowLayout {
        anchors.fill: parent; spacing: 0

        // Nav
        Rectangle {
            Layout.preferredWidth: 180; Layout.fillHeight: true
            color: "#2b2d31"

            ColumnLayout {
                anchors { fill: parent; topMargin: 16 }
                spacing: 2

                Repeater {
                    model: ["My Account", "Audio", "Advanced"]
                    delegate: Rectangle {
                        Layout.fillWidth: true; height: 36; radius: 4
                        color: navList.currentIndex === index ? "#3c3f45" : "transparent"
                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 12 }
                            text: modelData; color: navList.currentIndex === index ? "white" : "#b9bbbe"
                            font.pixelSize: 14
                        }
                        MouseArea { anchors.fill: parent; onClicked: navList.currentIndex = index; cursorShape: Qt.PointingHandCursor }
                    }
                }

                Item { Layout.fillHeight: true }

                // Logout
                Rectangle {
                    Layout.fillWidth: true; height: 36; radius: 4; color: "transparent"
                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 12 }
                        text: "Log Out"; color: "#ed4245"; font.pixelSize: 14
                    }
                    MouseArea { anchors.fill: parent; onClicked: { root.close(); authManager.logout() } }
                }
            }

            // Hidden ListView just for currentIndex tracking
            ListView { id: navList; model: 3; visible: false; currentIndex: 0 }
        }

        // Content
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: navList.currentIndex

            // My Account
            ScrollView {
                clip: true
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 24; width: parent.width - 48; spacing: 20
                    Item { implicitHeight: 12 }

                    // Avatar
                    RowLayout {
                        spacing: 16
                        AvatarBox {
                            size: 64
                            displayName: authManager.username
                            avatarUrl: appState.userProfile.avatarUrl ?? ""
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    avatarCropDlg.initialUrl = appState.userProfile.avatarUrl ?? ""
                                    avatarCropDlg.open()
                                }
                            }
                        }
                        ColumnLayout {
                            spacing: 4
                            Text { text: authManager.username; color: "white"; font { pixelSize: 18; bold: true } }
                            Text { text: "Click avatar to change"; color: "#72767d"; font.pixelSize: 12 }
                        }
                    }

                    // Display name
                    ColumnLayout { spacing: 6; Layout.fillWidth: true
                        Text { text: "DISPLAY NAME"; color: "#72767d"; font.pixelSize: 11 }
                        RowLayout { spacing: 8
                            TextField {
                                id: displayNameField
                                Layout.fillWidth: true
                                text: appState.userProfile.displayName ?? ""
                                placeholderText: "Display name"
                                background: Rectangle { color: "#1e1f22"; radius: 4 }
                                color: "white"; placeholderTextColor: "#72767d"
                            }
                            Button {
                                text: "Save"
                                onClicked: appState.updateDisplayName(displayNameField.text)
                            }
                        }
                    }
                }
            }

            // Audio
            ScrollView {
                clip: true
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 24; width: parent.width - 48; spacing: 20
                    Item { implicitHeight: 12 }

                    Text { text: "AUDIO SETTINGS"; color: "#72767d"; font.pixelSize: 11 }

                    ColumnLayout { spacing: 6; Layout.fillWidth: true
                        Text { text: "Input device"; color: "#dcddde"; font.pixelSize: 13 }
                        ComboBox {
                            id: inputDevBox; Layout.fillWidth: true
                            model: bridge.listInputDevices()
                        }
                    }

                    ColumnLayout { spacing: 6; Layout.fillWidth: true
                        Text { text: "Output device"; color: "#dcddde"; font.pixelSize: 13 }
                        ComboBox {
                            id: outputDevBox; Layout.fillWidth: true
                            model: bridge.listOutputDevices()
                        }
                    }

                    ColumnLayout { spacing: 6; Layout.fillWidth: true
                        RowLayout {
                            Text { text: "Master volume"; color: "#dcddde"; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: Math.round(volSlider.value * 100) + "%"; color: "#72767d"; font.pixelSize: 12 }
                        }
                        Slider { id: volSlider; from: 0; to: 2; value: 1.0; Layout.fillWidth: true
                            onValueChanged: bridge.setMasterVolume(value)
                        }
                    }

                    Button {
                        text: "Apply"
                        onClicked: {
                            bridge.setInputDevice(inputDevBox.currentText)
                            bridge.setOutputDevice(outputDevBox.currentText)
                        }
                    }
                }
            }

            // Advanced
            ScrollView {
                clip: true
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 24; width: parent.width - 48; spacing: 20
                    Item { implicitHeight: 12 }
                    Text { text: "No advanced settings yet."; color: "#72767d"; font.pixelSize: 14 }
                }
            }
        }
    }

    footer: DialogButtonBox {
        background: Rectangle { color: "#313338" }
        Button {
            text: "Close"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            background: Rectangle { color: parent.hovered ? "#4e5058" : "#3c3f45"; radius: 4 }
            contentItem: Text { text: parent.text; color: "white"; font: parent.font; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
