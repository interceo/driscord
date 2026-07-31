import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    color: "#f2f3f5"
    placeholderTextColor: "#80848e"
    selectionColor: "#5865f2"
    selectedTextColor: "#ffffff"
    font.pixelSize: 14

    background: Rectangle {
        radius: 4
        color: "#1e1f22"
        border.width: control.activeFocus ? 1 : 0
        border.color: "#00a8fc"
    }
}
