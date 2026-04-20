import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#313338"

    signal shareRequested

    // Build a unified model: local user + remote peers + a separate tile for
    // each streaming peer. Recomputed on connection/peer changes.
    function buildTiles() {
        var tiles = []
        if (appState.connectionState === "connected") {
            tiles.push({
                kind: "peer",
                id: bridge.localId(),
                username: authManager.username,
                avatarUrl: appState.userProfile.avatarUrl ?? "",
                isYou: true
            })
            for (var i = 0; i < appState.peers.length; i++) {
                var p = appState.peers[i]
                tiles.push({
                    kind: "peer",
                    id: p.id,
                    username: p.username && p.username.length > 0 ? p.username : p.id,
                    avatarUrl: p.avatarUrl ?? "",
                    isYou: false
                })
            }
            for (var j = 0; j < appState.streamingPeers.length; j++) {
                var sid = appState.streamingPeers[j]
                var sname = sid
                for (var k = 0; k < appState.peers.length; k++) {
                    if (appState.peers[k].id === sid) {
                        sname = appState.peers[k].username && appState.peers[k].username.length > 0
                                ? appState.peers[k].username : sid
                        break
                    }
                }
                tiles.push({ kind: "stream", id: sid, username: sname })
            }
        }
        return tiles
    }

    property var tiles: buildTiles()
    Connections {
        target: appState
        function onPeersChanged()          { root.tiles = root.buildTiles() }
        function onStreamingPeersChanged() { root.tiles = root.buildTiles() }
        function onConnectionChanged()     { root.tiles = root.buildTiles() }
        function onUserProfileChanged()    { root.tiles = root.buildTiles() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Toolbar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "#2b2d31"
            visible: appState.connected

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                spacing: 8

                Text {
                    text: qsTr("General")
                    color: "white"; font { pixelSize: 14; bold: true }
                    Layout.fillWidth: true
                }

                Button {
                    text: appState.sharing ? qsTr("Stop sharing") : qsTr("Share screen")
                    onClicked: appState.sharing ? appState.stopSharing() : root.shareRequested()
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#1e1f22"; visible: appState.connected }

        // Grid of tiles (peers + streams)
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors { fill: parent; margins: 16 }
                model: root.tiles
                cellWidth:  Math.max(280, width  / Math.max(1, Math.ceil(Math.sqrt(Math.max(1, count)))))
                cellHeight: Math.max(180, cellWidth * 9 / 16)
                clip: true
                visible: count > 0

                delegate: Rectangle {
                    id: tile
                    width: grid.cellWidth - 8
                    height: grid.cellHeight - 8
                    radius: 8
                    clip: true

                    readonly property bool isStream: modelData.kind === "stream"
                    readonly property bool isYou:    modelData.kind === "peer" && modelData.isYou
                    readonly property bool watching: isStream && appState.watchedPeerId === modelData.id

                    color: isStream ? "#111214" : "#1e1f22"
                    border.color: isYou ? "#5865f2" : "transparent"
                    border.width: isYou ? 1 : 0

                    // ---- Peer tile contents ----
                    AvatarBox {
                        anchors.centerIn: parent
                        size: Math.min(parent.width, parent.height) * 0.4
                        displayName: modelData.username ?? ""
                        avatarUrl: modelData.avatarUrl ?? ""
                        visible: !tile.isStream
                    }

                    // ---- Stream tile contents ----
                    Image {
                        id: videoImg
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        source: tile.watching ? ("image://frames/" + modelData.id) : ""
                        visible: tile.isStream && tile.watching && status === Image.Ready
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        visible: tile.isStream && !videoImg.visible
                        Text {
                            text: tile.watching ? qsTr("Buffering…") : "📺"
                            color: "#b9bbbe"
                            font.pixelSize: tile.watching ? 12 : 28
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            visible: !tile.watching
                            text: qsTr("Click to watch")
                            color: "#72767d"; font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    // LIVE badge (stream only)
                    Rectangle {
                        visible: tile.isStream
                        anchors { top: parent.top; right: parent.right; margins: 6 }
                        width: liveText.implicitWidth + 10
                        height: 16; radius: 3
                        color: "#ed4245"
                        Text {
                            id: liveText
                            anchors.centerIn: parent
                            text: "LIVE"; color: "white"
                            font { pixelSize: 9; bold: true }
                        }
                    }

                    // Name label
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 24
                        color: "#000000aa"
                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 }
                            text: tile.isYou ? (modelData.username + qsTr(" (you)")) : (modelData.username ?? "")
                            color: "white"; font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: tile.isStream
                        cursorShape: tile.isStream ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (tile.watching) appState.leaveStream()
                            else               appState.joinStream(modelData.id)
                        }
                    }

                    Connections {
                        target: bridge
                        enabled: tile.isStream
                        function onFrameUpdated(pid) {
                            if (pid === modelData.id && tile.watching) {
                                videoImg.source = ""
                                videoImg.source = "image://frames/" + modelData.id
                            }
                        }
                        function onFrameRemoved(pid) {
                            if (pid === modelData.id) videoImg.source = ""
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: !appState.connected || grid.count === 0
                text: appState.connected ? qsTr("No streams") : qsTr("Join a voice channel")
                color: "#72767d"; font.pixelSize: 18
            }
        }
    }
}
