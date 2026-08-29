import QtQuick
import QtQuick.Controls

ToolButton {
    id: root
    property color iconColor: "#dcddde"
    property int   iconSize: 18
    property color hoverColor: "#35373c"
    property color pressedColor: "#404249"
    property int   surfaceRadius: 4

    icon.color: iconColor
    icon.width: iconSize
    icon.height: iconSize

    background: Rectangle {
        radius: root.surfaceRadius
        color: root.down ? root.pressedColor
             : root.hovered ? root.hoverColor
             : "transparent"
    }
}
