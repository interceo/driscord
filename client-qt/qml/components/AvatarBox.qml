import QtQuick
import Qt5Compat.GraphicalEffects

Item {
    id: root
    property string avatarUrl: ""
    property string displayName: ""
    property int size: 32

    width: size; height: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.displayName.length > 0 ? hashColor(root.displayName) : "#5865f2"
        visible: avatarImg.status !== Image.Ready
        antialiasing: true

        Text {
            anchors.centerIn: parent
            text: root.displayName.charAt(0).toUpperCase()
            color: "white"
            font { pixelSize: root.size * 0.42; bold: true }
        }
    }

    Image {
        id: avatarImg
        anchors.fill: parent
        source: root.avatarUrl
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        smooth: true
        mipmap: true
        cache: true
        asynchronous: true
        visible: false
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: width / 2
        color: "white"
        antialiasing: true
        visible: false
        layer.enabled: true
        layer.smooth: true
    }

    OpacityMask {
        anchors.fill: parent
        source: avatarImg
        maskSource: mask
        antialiasing: true
        visible: avatarImg.status === Image.Ready
        cached: false
    }

    function hashColor(s) {
        var colors = ["#5865f2","#3ba55c","#faa61a","#ed4245","#eb459e","#9b59b6"]
        var h = 0
        for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) & 0xffff
        return colors[h % colors.length]
    }
}
