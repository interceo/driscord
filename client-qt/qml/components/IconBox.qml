import QtQuick
import QtQuick.Effects

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

    MultiEffect {
        anchors.fill: parent
        source: img
        colorization: 1.0
        colorizationColor: root.color
        visible: img.status === Image.Ready
    }
}
