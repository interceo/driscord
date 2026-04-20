import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#313338"

    // Video tiles grid
    GridView {
        id: grid
        anchors { fill: parent; margins: 16 }
        model: appState.streamingPeers
        cellWidth:  Math.max(320, width  / Math.max(1, Math.ceil(Math.sqrt(count))))
        cellHeight: Math.max(240, height / Math.max(1, Math.ceil(Math.sqrt(count))))
        clip: true

        delegate: VideoTile {
            width: grid.cellWidth - 8
            height: grid.cellHeight - 8
            peerId: modelData
            displayName: {
                var peers = appState.peers
                for (var i = 0; i < peers.length; i++)
                    if (peers[i].id === modelData) return peers[i].username
                return modelData
            }
            avatarUrl: {
                var peers = appState.peers
                for (var i = 0; i < peers.length; i++)
                    if (peers[i].id === modelData) return peers[i].avatarUrl ?? ""
                return ""
            }
        }
    }

    // Empty state
    Text {
        anchors.centerIn: parent
        text: appState.connected ? "No streams" : "Join a voice channel"
        color: "#72767d"; font.pixelSize: 18
        visible: appState.streamingPeers.length === 0
    }
}
