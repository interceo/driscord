import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

DiscordDialog {
    id: root
    title: qsTr("Invite people")
    width: 440
    height: 520

    property string query: ""

    onOpened: {
        query = ""
        searchField.text = ""
        appState.loadUsers()
        searchField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: qsTr("Add a registered Driscord user to this server.")
            color: "#b5bac1"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        DiscordTextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search by name or username")
            onTextChanged: root.query = text.trim().toLowerCase()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2b2d31"
            radius: 6

            ListView {
                id: usersList
                anchors { fill: parent; margins: 6 }
                clip: true
                spacing: 2
                model: appState.users.filter(function(user) {
                    if (root.query === "") return true
                    return user.username.toLowerCase().includes(root.query)
                        || user.displayName.toLowerCase().includes(root.query)
                })

                delegate: Rectangle {
                    required property var modelData
                    width: usersList.width
                    height: 56
                    radius: 4
                    color: rowHover.hovered ? "#35373c" : "transparent"

                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                        spacing: 10

                        AvatarBox {
                            size: 34
                            displayName: modelData.displayName
                            avatarUrl: modelData.avatarUrl
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: modelData.displayName
                                color: "#f2f3f5"
                                font { pixelSize: 14; weight: Font.DemiBold }
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "@" + modelData.username
                                color: "#949ba4"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        DiscordButton {
                            text: qsTr("Add")
                            implicitWidth: 72
                            implicitHeight: 32
                            onClicked: appState.inviteUser(modelData.id)
                        }
                    }

                }

                ScrollBar.vertical: ScrollBar { }
            }

            Text {
                anchors.centerIn: parent
                visible: usersList.count === 0
                text: root.query === "" ? qsTr("No users available") : qsTr("No users found")
                color: "#949ba4"
                font.pixelSize: 13
            }
        }
    }

    footer: DialogButtonBox {
        leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
        background: Rectangle { color: "#2b2d31"; radius: 8 }
        DiscordButton {
            text: qsTr("Done")
            variant: "secondary"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
