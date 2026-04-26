import QtQuick
import Qt5Compat.GraphicalEffects

Item {
    id: root
    property string source: ""
    property color color: "#dcddde"
    property int size: 16

    width: size; height: size

    Image {
        id: img
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    ColorOverlay {
        anchors.fill: parent
        source: img
        color: root.color
        visible: img.status === Image.Ready
    }
}
