import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#1e1f22"

    ColumnLayout {
        anchors { top: parent.top; bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; topMargin: 12 }
        spacing: 8

        Repeater {
            model: appState.servers
            delegate: Rectangle {
                width: 48; height: 48
                radius: appState.selectedServerId === modelData.id ? 16 : 24
                color: appState.selectedServerId === modelData.id ? "#5865f2" : "#313338"

                Behavior on radius { NumberAnimation { duration: 150 } }

                Text {
                    anchors.centerIn: parent
                    text: modelData.name.charAt(0).toUpperCase()
                    color: "white"
                    font { pixelSize: 18; bold: true }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: appState.selectServer(modelData.id)
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        // Add server
        Rectangle {
            width: 48; height: 48; radius: 24
            color: addArea.containsMouse ? "#3ba55c" : "#313338"
            Behavior on color { ColorAnimation { duration: 120 } }
            Text {
                anchors.centerIn: parent; text: "+"; color: addArea.containsMouse ? "white" : "#3ba55c"
                font.pixelSize: 24
            }
            MouseArea {
                id: addArea; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: createServerDialog.open()
            }
        }

        Item { Layout.fillHeight: true }
    }
}
