import QtQuick
import QtQuick.Controls
import QtMultimedia

Rectangle {
    id: root
    property string peerId: ""
    property string displayName: ""
    property string avatarUrl: ""
    // True once the sink received a real frame; drives the avatar fallback.
    property bool hasVideo: false

    color: "#111214"
    radius: 8
    clip: true

    // Frames arrive straight from the decoder thread through the sink the
    // bridge holds; YUV->RGB happens in the render shader.
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

    // The id the sink is currently registered under: peerId has already
    // changed by the time onPeerIdChanged runs, so the old binding must be
    // released by its remembered name.
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

    // Fallback avatar when no stream
    AvatarBox {
        anchors.centerIn: parent
        size: Math.min(root.width, root.height) * 0.4
        displayName: root.displayName
        avatarUrl: root.avatarUrl
        visible: !root.hasVideo
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
}
