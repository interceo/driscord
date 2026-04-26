import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: qsTr("Share Screen")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 620; height: 480
    padding: 0

    property var targets: []
    property int selectedIndex: -1

    readonly property var qualityPresets: [
        { label: "1080p", w: 1920, h: 1080 },
        { label: "720p",  w: 1280, h: 720  },
        { label: "480p",  w: 854,  h: 480  },
        { label: "360p",  w: 640,  h: 360  },
    ]
    readonly property var fpsPresets: [30, 60]

    onOpened: {
        var json = appState.captureVideoTargetsJson()
        var parsed = JSON.parse(json)
        for (var i = 0; i < parsed.length; i++) {
            parsed[i].thumbUrl = bridge.grabThumbnail(JSON.stringify(parsed[i]), 240, 150)
        }
        targets = parsed
        selectedIndex = -1
    }

    background: Rectangle {
        color: "#2b2d31"
        radius: 8
        border { color: "#1e1f22"; width: 1 }
    }

    header: Rectangle {
        color: "#1e1f22"
        height: 44
        radius: 8
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.radius
            color: parent.color
        }
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 16 }
            text: root.title; color: "white"; font { pixelSize: 14; bold: true }
        }
    }

    // --- Reusable dark dropdown ----------------------------------------------
    component DarkCombo : Item {
        id: combo
        property var options: []
        property int currentIndex: 0
        property string label: ""
        property int boxWidth: 80
        readonly property string currentText:
            options.length > 0 ? (options[currentIndex].label ?? options[currentIndex]) : ""

        implicitWidth: boxWidth
        implicitHeight: 38

        ColumnLayout {
            anchors.fill: parent
            spacing: 2

            Text {
                text: combo.label
                color: "#72767d"; font.pixelSize: 10
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                color: trigger.containsMouse ? "#3c3f45" : "#1e1f22"
                radius: 4
                border { color: "#3c3f45"; width: 1 }

                RowLayout {
                    anchors { fill: parent; leftMargin: 6; rightMargin: 4 }
                    spacing: 0
                    Text {
                        text: combo.currentText
                        color: "#dcddde"; font.pixelSize: 11
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text { text: "▾"; color: "#72767d"; font.pixelSize: 9 }
                }

                MouseArea {
                    id: trigger
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: popup.open()
                }
            }
        }

        Popup {
            id: popup
            y: combo.height + 2
            x: 0
            width: combo.width
            padding: 1
            background: Rectangle {
                color: "#2b2d31"
                border { color: "#1e1f22"; width: 1 }
                radius: 4
            }
            contentItem: ListView {
                implicitHeight: contentHeight
                model: combo.options
                spacing: 0
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 26
                    color: itemArea.containsMouse ? "#3c3f45" : "transparent"
                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 }
                        text: (modelData.label ?? modelData)
                        color: "#dcddde"; font.pixelSize: 11
                    }
                    MouseArea {
                        id: itemArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            combo.currentIndex = index
                            popup.close()
                        }
                    }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        GridView {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: 12
            model: root.targets
            cellWidth: 168; cellHeight: 130
            clip: true

            delegate: Item {
                width: 160; height: 122

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    readonly property bool selected: root.selectedIndex === index
                    color: selected ? "#3c3f45" : (targetArea.containsMouse ? "#33363c" : "#1e1f22")
                    border {
                        color: selected ? "#5865f2"
                                        : (targetArea.containsMouse ? "#3c3f45" : "transparent")
                        width: selected ? 2 : 1
                    }

                    ColumnLayout {
                        anchors { fill: parent; margins: 6 }
                        spacing: 4

                        Image {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            source: modelData.thumbUrl ?? ""
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            asynchronous: true
                            smooth: true
                        }

                        Text {
                            text: modelData.name ?? ""
                            color: "#dcddde"; font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: targetArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedIndex = index
                        onDoubleClicked: {
                            root.selectedIndex = index
                            goLive()
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        color: "#2b2d31"
        height: 64
        radius: 8
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: parent.radius
            color: parent.color
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1; color: "#1e1f22"
        }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 16; rightMargin: 16
                topMargin: 8;   bottomMargin: 8
            }
            spacing: 12

            DarkCombo {
                id: qualityCombo
                label: qsTr("Quality")
                boxWidth: 80
                options: root.qualityPresets
                currentIndex: 0
            }
            DarkCombo {
                id: fpsCombo
                label: qsTr("FPS")
                boxWidth: 60
                options: root.fpsPresets
                currentIndex: 0
            }

            // Share Audio checkbox
            Item {
                id: audioCheck
                Layout.preferredHeight: 22
                Layout.preferredWidth: shareAudioRow.implicitWidth + 4
                Layout.alignment: Qt.AlignVCenter
                Layout.topMargin: 14
                visible: bridge.captureAudioTargetsJson() !== "[]"

                property bool checked: true

                RowLayout {
                    id: shareAudioRow
                    anchors.fill: parent
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 16; Layout.preferredHeight: 16
                        radius: 3
                        color: audioCheck.checked ? "#5865f2" : "transparent"
                        border {
                            color: audioCheck.checked ? "#5865f2" : "#72767d"
                            width: 1
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: audioCheck.checked
                            text: "✓"; color: "white"; font { pixelSize: 11; bold: true }
                        }
                    }
                    Text {
                        text: qsTr("Share Audio")
                        color: "#dcddde"; font.pixelSize: 13
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: audioCheck.checked = !audioCheck.checked
                }
            }

            Item { Layout.fillWidth: true }

            // Cancel
            Rectangle {
                Layout.preferredWidth: 80; Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                Layout.topMargin: 14
                radius: 4
                color: "transparent"
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Cancel")
                    color: cancelArea.containsMouse ? "white" : "#b9bbbe"
                    font.pixelSize: 13
                }
                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.reject()
                }
            }

            // Go Live
            Rectangle {
                Layout.preferredWidth: 96; Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                Layout.topMargin: 14
                radius: 4
                readonly property bool enabled: root.selectedIndex >= 0
                color: enabled
                       ? (goLiveArea.containsMouse ? "#4752c4" : "#5865f2")
                       : "#3c3f45"
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Go Live")
                    color: parent.enabled ? "white" : "#72767d"
                    font { pixelSize: 13; bold: true }
                }
                MouseArea {
                    id: goLiveArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: parent.enabled
                    cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.goLive()
                }
            }
        }
    }

    function goLive() {
        if (root.selectedIndex < 0) return
        var target = root.targets[root.selectedIndex]
        var q = root.qualityPresets[qualityCombo.currentIndex]
        var fps = root.fpsPresets[fpsCombo.currentIndex]
        appState.startSharing(JSON.stringify(target), q.w, q.h, fps, audioCheck.checked)
        root.close()
    }
}
