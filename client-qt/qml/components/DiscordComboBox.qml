import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 34
    font.pixelSize: 14

    contentItem: Text {
        text: control.displayText
        color: "#f2f3f5"
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: "#b5bac1"
        font.pixelSize: 11
    }
    background: Rectangle {
        radius: 4
        color: "#1e1f22"
        border.width: control.activeFocus ? 1 : 0
        border.color: "#00a8fc"
    }
    delegate: ItemDelegate {
        required property var modelData
        width: control.width
        height: 36
        leftPadding: 10
        contentItem: Text {
            text: modelData
            color: "#dbdee1"
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.highlighted ? "#404249" : "transparent"
            radius: 3
        }
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 4
        implicitHeight: contentItem.implicitHeight + 8
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle {
            color: "#111214"
            radius: 5
            border.color: "#1e1f22"
        }
    }
}
