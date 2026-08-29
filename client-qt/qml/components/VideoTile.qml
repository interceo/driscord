import QtQuick
import QtQuick.Controls
import QtMultimedia

Rectangle {
    id: root
    property string peerId: ""
    property string displayName: ""
    property string avatarUrl: ""
    property bool hasVideo: false

    color: "#111214"
    radius: 8
    clip: true

    VideoOutput {
        id: videoOut
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        visible: root.hasVideo
    }

    Connections {
        target: videoOut.videoSink
        function onVideoFrameChanged() {
            root.hasVideo = videoOut.videoSink.videoFrame.isValid
        }
    }

    property string boundPeerId: ""
    function rebindSink() {
        if (boundPeerId !== "")
            bridge.unregisterVideoSink(boundPeerId, videoOut.videoSink)
        boundPeerId = peerId
        hasVideo = false
        if (boundPeerId !== "")
            bridge.registerVideoSink(boundPeerId, videoOut.videoSink)
    }
    onPeerIdChanged: rebindSink()
    Component.onCompleted: rebindSink()
    Component.onDestruction: if (boundPeerId !== "")
        bridge.unregisterVideoSink(boundPeerId, videoOut.videoSink)

    AvatarBox {
        anchors.centerIn: parent
        size: Math.min(root.width, root.height) * 0.4
        displayName: root.displayName
        avatarUrl: root.avatarUrl
        visible: !root.hasVideo
    }

    Text {
        anchors { bottom: parent.bottom; left: parent.left; margins: 8 }
        text: root.displayName
        color: "white"
        font.pixelSize: 13
        style: Text.Outline
        styleColor: "black"
    }
}
