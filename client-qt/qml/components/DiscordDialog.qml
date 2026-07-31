import QtQuick
import QtQuick.Controls

Dialog {
    id: control
    modal: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    topPadding: 18
    bottomPadding: 18
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: "#00000099" }
    background: Rectangle {
        color: "#313338"
        radius: 8
        border.color: "#1e1f22"
        border.width: 1
    }
    header: Rectangle {
        implicitHeight: 58
        color: "#313338"
        radius: 8
        Text {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            leftPadding: 20
            rightPadding: 20
            text: control.title
            color: "#f2f3f5"
            font { pixelSize: 18; weight: Font.DemiBold }
            elide: Text.ElideRight
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: "#2b2d31"
        }
    }
}
