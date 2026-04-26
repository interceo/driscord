import QtQuick
import QtQuick.Controls

// Round-rect icon button with a dark hover surface (no white flash from the
// default Qt Controls style). Drop-in replacement for ToolButton for icons.
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
