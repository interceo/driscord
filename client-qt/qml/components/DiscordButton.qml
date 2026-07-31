import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string variant: "primary"

    implicitHeight: 38
    leftPadding: 16
    rightPadding: 16
    font { pixelSize: 14; weight: Font.DemiBold }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? "#ffffff" : "#949ba4"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 4
        color: {
            if (!control.enabled) return "#3f4147"
            if (control.variant === "secondary")
                return control.down ? "#35373c" : control.hovered ? "#4e5058" : "transparent"
            if (control.variant === "danger")
                return control.down ? "#a1282c" : control.hovered ? "#c03537" : "#da373c"
            return control.down ? "#3c45a5" : control.hovered ? "#4752c4" : "#5865f2"
        }
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
