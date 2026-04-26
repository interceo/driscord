import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property string peerId: ""
    property string displayName: ""
    property string avatarUrl: ""

    color: "#111214"
    radius: 8
    clip: true

    // Live video frame from FrameProvider
    Image {
        id: videoImg
        anchors.fill: parent
        source: root.peerId !== "" ? ("image://frames/" + root.peerId) : ""
        fillMode: Image.PreserveAspectFit
        cache: false
        visible: status === Image.Ready
    }

    // Fallback avatar when no stream
    AvatarBox {
        anchors.centerIn: parent
        size: Math.min(root.width, root.height) * 0.4
        displayName: root.displayName
        avatarUrl: root.avatarUrl
        visible: videoImg.status !== Image.Ready
    }

    // Name label
    Text {
        anchors { bottom: parent.bottom; left: parent.left; margins: 8 }
        text: root.displayName
        color: "white"
        font.pixelSize: 13
        style: Text.Outline
        styleColor: "black"
    }

    // Update image when frame arrives
    Connections {
        target: bridge
        function onFrameUpdated(pid) {
            if (pid === root.peerId) videoImg.source = ""  // force reload
            videoImg.source = "image://frames/" + root.peerId
        }
        function onFrameRemoved(pid) {
            if (pid === root.peerId) videoImg.source = ""
        }
    }
}
