import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    title: "Set Avatar"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 400; height: 580
    background: Rectangle { color: "#313338" }
    header: Item {
        height: 42
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 16 }
            text: "Set Avatar"; color: "white"; font { pixelSize: 15; bold: true }
        }
    }

    property string imagePath: ""
    property string initialUrl: ""   // existing avatar URL shown before user picks a file
    property real   cropScale: 1.0
    property real   offsetX: 0
    property real   offsetY: 0

    onOpened: {
        root.imagePath = ""
        root.offsetX   = 0
        root.offsetY   = 0
    }

    FileDialog {
        id: filePicker
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: {
            root.initialUrl = ""
            root.imagePath = selectedFile.toString().replace("file://", "")
        }
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 16

        Button {
            text: "Choose image..."
            Layout.alignment: Qt.AlignHCenter
            background: Rectangle { color: parent.hovered ? "#4e5058" : "#3c3f45"; radius: 4 }
            contentItem: Text { text: parent.text; color: "white"; font: parent.font; horizontalAlignment: Text.AlignHCenter }
            onClicked: filePicker.open()
        }

        // Visible preview: full image + semi-transparent overlay with circular cutout
        Item {
            id: previewArea
            Layout.alignment: Qt.AlignHCenter
            width: 320; height: 320
            clip: true

            Rectangle {
                anchors.fill: parent
                color: "#1e1f22"
            }

            Image {
                id: cropImg
                source: root.imagePath !== "" ? ("file://" + root.imagePath)
                      : root.initialUrl !== "" ? root.initialUrl : ""
                width:  sourceSize.width  * root.cropScale
                height: sourceSize.height * root.cropScale
                x: root.offsetX + (previewArea.width  - width)  / 2
                y: root.offsetY + (previewArea.height - height) / 2
                fillMode: Image.Stretch
                smooth: true
                onSourceSizeChanged: {
                    if (sourceSize.width <= 0 || sourceSize.height <= 0) return
                    root.offsetX = 0
                    root.offsetY = 0
                    var minSide = Math.min(sourceSize.width, sourceSize.height)
                    var fitScale = 256.0 / minSide
                    cropScaleSlider.from  = fitScale
                    cropScaleSlider.to    = fitScale * 3.0
                    cropScaleSlider.value = fitScale
                }
            }

            MouseArea {
                anchors.fill: parent
                property real lastX: 0
                property real lastY: 0
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                onPressed:  (e) => { lastX = e.x; lastY = e.y }
                onPositionChanged: (e) => {
                    root.offsetX += e.x - lastX
                    root.offsetY += e.y - lastY
                    lastX = e.x; lastY = e.y
                }
                onWheel: (e) => {
                    var factor = 1.0 + e.angleDelta.y / 800.0
                    var next = cropScaleSlider.value * factor
                    cropScaleSlider.value = Math.max(cropScaleSlider.from,
                                           Math.min(cropScaleSlider.to, next))
                }
            }

            // Semi-transparent dark overlay with circular cutout
            Canvas {
                anchors.fill: parent
                Component.onCompleted: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = "rgba(0, 0, 0, 0.65)"
                    ctx.fillRect(0, 0, width, height)
                    ctx.globalCompositeOperation = "destination-out"
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, 128, 0, Math.PI * 2)
                    ctx.fill()
                    ctx.globalCompositeOperation = "source-over"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Zoom"; color: "#b9bbbe"; font.pixelSize: 12 }
            Slider {
                id: cropScaleSlider
                Layout.fillWidth: true
                from: 0.1; to: 8.0; value: 1.0
                onValueChanged: root.cropScale = value
            }
        }
    }

    footer: DialogButtonBox {
        background: Rectangle { color: "#313338" }
        Button {
            text: "Cancel"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            background: Rectangle { color: parent.hovered ? "#4e5058" : "#3c3f45"; radius: 4 }
            contentItem: Text { text: parent.text; color: "white"; font: parent.font; horizontalAlignment: Text.AlignHCenter }
        }
        Button {
            text: "Save"
            enabled: root.imagePath !== "" || root.initialUrl !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            background: Rectangle { color: parent.enabled ? (parent.hovered ? "#3e6de8" : "#5865f2") : "#4e5058"; radius: 4 }
            contentItem: Text { text: parent.text; color: "white"; font: parent.font; horizontalAlignment: Text.AlignHCenter }
        }
    }

    onAccepted: {
        appState.uploadAvatarCropped(root.imagePath, root.cropScale, root.offsetX, root.offsetY)
    }
}
