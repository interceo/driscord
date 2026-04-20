import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "Share Screen"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 560; height: 420

    property var targets: []

    onOpened: {
        var json = appState.captureVideoTargetsJson()
        targets = JSON.parse(json)
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 12

        GridView {
            Layout.fillWidth: true; Layout.fillHeight: true
            model: root.targets
            cellWidth: 160; cellHeight: 130
            clip: true

            delegate: Rectangle {
                width: 152; height: 122; radius: 6
                color: targetArea.containsMouse ? "#3c3f45" : "#1e1f22"

                ColumnLayout {
                    anchors { fill: parent; margins: 8 }
                    spacing: 6

                    // Thumbnail
                    Image {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        source: modelData.thumbnail_url ?? ""
                        fillMode: Image.PreserveAspectFit
                    }

                    Text {
                        text: modelData.name ?? ""
                        color: "white"; font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                MouseArea {
                    id: targetArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        appState.startSharing(JSON.stringify(modelData), 1920, 1080, 30, audioCheck.checked)
                        root.close()
                    }
                }
            }
        }

        CheckBox {
            id: audioCheck
            text: "Share system audio"
            visible: bridge.captureAudioTargetsJson() !== "[]"
        }
    }

    footer: DialogButtonBox {
        Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
}
