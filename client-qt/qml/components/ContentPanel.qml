import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#313338"

    // Inline-expanded tile state — empty when nothing is expanded.
    // Expansion happens within the content panel (does not cover the sidebar).
    // expandedKind is "stream" (video) or "peer" (avatar+name); the same id can
    // appear as both, so kind is part of the identity.
    property string expandedKind:      ""
    property string expandedPeerId:    ""
    property string expandedPeerName:  ""
    property string expandedAvatarUrl: ""
    property bool   expandedIsYou:     false
    property bool   expandedHasFrame:  false

    function toggleExpand(kind, peerId, peerName, avatarUrl, isYou) {
        if (root.expandedPeerId === peerId && root.expandedKind === kind) {
            root.collapseExpanded()
            return
        }
        if (kind === "stream" && appState.watchedPeerId !== peerId) {
            appState.joinStream(peerId)
        }
        root.expandedHasFrame  = false
        root.expandedKind      = kind
        root.expandedPeerId    = peerId
        root.expandedPeerName  = peerName
        root.expandedAvatarUrl = avatarUrl ?? ""
        root.expandedIsYou     = isYou ?? false
    }
    function collapseExpanded() {
        root.expandedKind      = ""
        root.expandedPeerId    = ""
        root.expandedPeerName  = ""
        root.expandedAvatarUrl = ""
        root.expandedIsYou     = false
        root.expandedHasFrame  = false
    }

    // Pick a deterministic accent color for a peer based on their displayed
    // name — same palette/algorithm as AvatarBox so the tile background and
    // the avatar fallback agree when no avatar image is loaded.
    function peerAccentColor(name) {
        var colors = ["#5865f2","#3ba55c","#faa61a","#ed4245","#eb459e","#9b59b6"]
        if (!name || name.length === 0) return "#1e1f22"
        var h = 0
        for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) & 0xffff
        return colors[h % colors.length]
    }

    // Pick the best human-readable label for a peer: display name → username → id.
    function peerLabel(displayName, username, id) {
        if (displayName && displayName.length > 0) return displayName
        if (username && username.length > 0)       return username
        return id ?? ""
    }

    // Build a unified model: local user + remote peers + a separate tile for
    // each streaming peer. Recomputed on connection/peer changes.
    function buildTiles() {
        var tiles = []
        if (appState.connectionState === "connected") {
            var localDisplay = (appState.userProfile.displayName
                                && appState.userProfile.displayName.length > 0)
                               ? appState.userProfile.displayName
                               : authManager.displayName
            tiles.push({
                kind: "peer",
                id: bridge.localId(),
                username: authManager.username,
                displayName: localDisplay,
                avatarUrl: appState.userProfile.avatarUrl ?? "",
                isYou: true
            })
            for (var i = 0; i < appState.peers.length; i++) {
                var p = appState.peers[i]
                tiles.push({
                    kind: "peer",
                    id: p.id,
                    username: p.username ?? "",
                    displayName: root.peerLabel(p.displayName, p.username, p.id),
                    avatarUrl: p.avatarUrl ?? "",
                    isYou: false
                })
            }
            for (var j = 0; j < appState.streamingPeers.length; j++) {
                var sid = appState.streamingPeers[j]
                var sLabel = sid, sUsername = ""
                for (var k = 0; k < appState.peers.length; k++) {
                    if (appState.peers[k].id === sid) {
                        sUsername = appState.peers[k].username ?? ""
                        sLabel = root.peerLabel(appState.peers[k].displayName,
                                                appState.peers[k].username, sid)
                        break
                    }
                }
                tiles.push({ kind: "stream", id: sid,
                             username: sUsername, displayName: sLabel })
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
                visible: count > 0 && root.expandedKind === ""

                delegate: Rectangle {
                    id: tile
                    width: grid.cellWidth - 8
                    height: grid.cellHeight - 8
                    radius: 8
                    clip: true

                    readonly property bool isStream: modelData.kind === "stream"
                    readonly property bool isYou:    modelData.kind === "peer" && modelData.isYou
                    readonly property bool watching: isStream && appState.watchedPeerId === modelData.id
                    property bool hasFrame: false
                    onWatchingChanged: if (!watching) hasFrame = false

                    // Peer tile background: a single solid color sampled from
                    // the avatar (1×1 downscale, computed once in C++ and cached).
                    // Falls back to a hash-of-name accent until the tint resolves
                    // or when no avatar URL is present.
                    property color peerTint: !tile.isStream
                            ? avatarTint.colorFor(modelData.avatarUrl ?? "")
                            : "transparent"

                    color: tile.isStream
                           ? "#111214"
                           : (tile.peerTint.a > 0
                              ? tile.peerTint
                              : root.peerAccentColor(modelData.displayName ?? modelData.username ?? ""))
                    border.color: isYou ? "#5865f2" : "transparent"
                    border.width: isYou ? 1 : 0

                    Component.onCompleted: {
                        if (!tile.isStream && (modelData.avatarUrl ?? "") !== "")
                            avatarTint.prefetch(modelData.avatarUrl)
                    }
                    Connections {
                        target: avatarTint
                        enabled: !tile.isStream
                        function onColorReady(url, color) {
                            if (url === (modelData.avatarUrl ?? "")) tile.peerTint = color
                        }
                    }

                    // ---- Peer tile contents ----
                    AvatarBox {
                        anchors.centerIn: parent
                        size: Math.min(parent.width, parent.height) * 0.4
                        displayName: modelData.displayName ?? modelData.username ?? ""
                        avatarUrl: modelData.avatarUrl ?? ""
                        visible: !tile.isStream
                    }

                    // ---- Stream tile contents ----
                    Image {
                        id: videoImg
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        source: tile.watching && tile.hasFrame ? ("image://frames/" + modelData.id) : ""
                        visible: tile.isStream && tile.watching && tile.hasFrame && status === Image.Ready
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        visible: tile.isStream && !videoImg.visible
                        Text {
                            visible: tile.watching
                            text: qsTr("Buffering…")
                            color: "#b9bbbe"; font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                        }
                        IconBox {
                            visible: !tile.watching
                            source: "qrc:/icons/monitor.svg"
                            color: "#b9bbbe"
                            size: 32
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
                            text: tile.isYou
                                  ? ((modelData.displayName ?? modelData.username ?? "") + qsTr(" (you)"))
                                  : (modelData.displayName ?? modelData.username ?? "")
                            color: "white"; font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                // Leave-stream confirm only for stream tiles the user is watching.
                                if (tile.isStream && tile.watching) {
                                    leaveConfirm.peerId   = modelData.id
                                    leaveConfirm.peerName = modelData.displayName ?? modelData.username ?? ""
                                    leaveConfirm.open()
                                }
                            } else {
                                root.toggleExpand(
                                    modelData.kind,
                                    modelData.id,
                                    modelData.displayName ?? modelData.username ?? "",
                                    modelData.avatarUrl ?? "",
                                    modelData.isYou ?? false)
                            }
                        }
                    }

                    Connections {
                        target: bridge
                        enabled: tile.isStream
                        function onFrameUpdated(pid) {
                            if (pid === modelData.id && tile.watching) {
                                tile.hasFrame = true
                                videoImg.source = ""
                                videoImg.source = "image://frames/" + modelData.id
                            }
                        }
                        function onFrameRemoved(pid) {
                            if (pid === modelData.id) {
                                tile.hasFrame = false
                                videoImg.source = ""
                            }
                        }
                    }
                }
            }

            // Inline-expanded tile view — fills the grid area while the
            // sidebar / toolbar remain visible. Click anywhere to collapse.
            // Renders a video for streams or a large avatar+name for peers.
            Item {
                id: expandedView
                anchors { fill: parent; margins: 16 }
                visible: root.expandedKind !== ""

                readonly property bool isStream: root.expandedKind === "stream"
                readonly property bool isPeer:   root.expandedKind === "peer"

                Rectangle {
                    id: expandedRect
                    // Fit a 16:9 box within the available area, preserving the
                    // grid tile's aspect ratio instead of stretching to fill.
                    anchors.centerIn: parent
                    width:  Math.min(parent.width,  parent.height * 16 / 9)
                    height: Math.min(parent.height, parent.width  *  9 / 16)
                    radius: 8

                    // Same avatar-derived tint pattern as the grid peer tile.
                    property color peerTint: expandedView.isPeer
                            ? avatarTint.colorFor(root.expandedAvatarUrl)
                            : "transparent"

                    color: expandedView.isStream
                           ? "#111214"
                           : (expandedRect.peerTint.a > 0
                              ? expandedRect.peerTint
                              : root.peerAccentColor(root.expandedPeerName))
                    border.color: expandedView.isPeer && root.expandedIsYou ? "#5865f2" : "transparent"
                    border.width: expandedView.isPeer && root.expandedIsYou ? 1 : 0
                    clip: true

                    Component.onCompleted: {
                        if (expandedView.isPeer && root.expandedAvatarUrl !== "")
                            avatarTint.prefetch(root.expandedAvatarUrl)
                    }
                    Connections {
                        target: avatarTint
                        enabled: expandedView.isPeer
                        function onColorReady(url, color) {
                            if (url === root.expandedAvatarUrl) expandedRect.peerTint = color
                        }
                    }

                    // ---- Stream content: live video frame ----
                    Image {
                        id: expandedImage
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        source: expandedView.isStream && root.expandedHasFrame
                                ? ("image://frames/" + root.expandedPeerId) : ""
                        visible: expandedView.isStream && status === Image.Ready
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: expandedView.isStream && !expandedImage.visible
                        text: qsTr("Buffering…")
                        color: "#b9bbbe"
                        font.pixelSize: 18
                    }

                    // ---- Peer content: large centered avatar ----
                    AvatarBox {
                        anchors.centerIn: parent
                        size: Math.min(parent.width, parent.height) * 0.4
                        displayName: root.expandedPeerName
                        avatarUrl: root.expandedAvatarUrl
                        visible: expandedView.isPeer
                    }

                    // Name label (bottom strip) — same style as grid tile.
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 28
                        color: "#000000aa"
                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 12 }
                            text: expandedView.isPeer && root.expandedIsYou
                                  ? (root.expandedPeerName + qsTr(" (you)"))
                                  : root.expandedPeerName
                            color: "white"; font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }

                    // LIVE badge (top-right) — streams only.
                    Rectangle {
                        visible: expandedView.isStream
                        anchors { top: parent.top; right: parent.right; margins: 12 }
                        width: expLiveText.implicitWidth + 12
                        height: 18; radius: 3
                        color: "#ed4245"
                        Text {
                            id: expLiveText
                            anchors.centerIn: parent
                            text: "LIVE"; color: "white"
                            font { pixelSize: 10; bold: true }
                        }
                    }

                    // LMB anywhere collapses. RMB opens leave dialog only for streams.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                if (expandedView.isStream) {
                                    leaveConfirm.peerId   = root.expandedPeerId
                                    leaveConfirm.peerName = root.expandedPeerName
                                    leaveConfirm.open()
                                }
                            } else {
                                root.collapseExpanded()
                            }
                        }
                    }
                }

                // Live frame updates only matter for stream expansion.
                Connections {
                    target: bridge
                    enabled: expandedView.isStream
                    function onFrameUpdated(pid) {
                        if (pid !== root.expandedPeerId) return
                        root.expandedHasFrame = true
                        expandedImage.source = ""
                        expandedImage.source = "image://frames/" + root.expandedPeerId
                    }
                    function onFrameRemoved(pid) {
                        if (pid === root.expandedPeerId) {
                            root.expandedHasFrame = false
                            expandedImage.source = ""
                        }
                    }
                }

                // Auto-collapse stream expansion if the stream goes away or is
                // left elsewhere. Peer expansion is collapsed when the peer leaves.
                Connections {
                    target: appState
                    function onWatchedPeerChanged() {
                        if (expandedView.isStream
                            && appState.watchedPeerId !== root.expandedPeerId) {
                            root.collapseExpanded()
                        }
                    }
                    function onStreamingPeersChanged() {
                        if (!expandedView.isStream) return
                        var still = false
                        for (var i = 0; i < appState.streamingPeers.length; i++) {
                            if (appState.streamingPeers[i] === root.expandedPeerId) {
                                still = true; break
                            }
                        }
                        if (!still) root.collapseExpanded()
                    }
                    function onPeersChanged() {
                        if (!expandedView.isPeer) return
                        if (root.expandedIsYou) return
                        var still = false
                        for (var i = 0; i < appState.peers.length; i++) {
                            if (appState.peers[i].id === root.expandedPeerId) {
                                still = true; break
                            }
                        }
                        if (!still) root.collapseExpanded()
                    }
                    function onConnectionChanged() {
                        if (!appState.connected) root.collapseExpanded()
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: (!appState.connected || grid.count === 0) && root.expandedKind === ""
                text: appState.connected ? qsTr("No streams") : qsTr("Join a voice channel")
                color: "#72767d"; font.pixelSize: 18
            }
        }
    }

    // ---- Leave-stream confirmation (Discord-styled) ----
    Popup {
        id: leaveConfirm
        property string peerId: ""
        property string peerName: ""

        function accept() {
            if (appState.watchedPeerId === peerId) appState.leaveStream()
            if (root.expandedKind === "stream"
                && root.expandedPeerId === peerId) {
                root.collapseExpanded()
            }
            close()
        }

        parent: Overlay.overlay
        x: parent ? (parent.width  - width)  / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        width: 440
        modal: true
        padding: 0
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // Backdrop dimming, like Discord's modal scrim.
        Overlay.modal: Rectangle { color: "#000000"; opacity: 0.6 }

        background: Rectangle {
            color: "#313338"
            radius: 6
            border.color: "#1e1f22"
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 0
            focus: true
            Keys.onReturnPressed: leaveConfirm.accept()
            Keys.onEnterPressed:  leaveConfirm.accept()

            // Header
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 8

                Text {
                    text: qsTr("Leave stream?")
                    color: "white"
                    font { pixelSize: 20; bold: true }
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Text {
                    text: leaveConfirm.peerName.length > 0
                          ? qsTr("Are you sure you want to stop watching %1's stream?")
                                .arg(leaveConfirm.peerName)
                          : qsTr("Are you sure you want to stop watching this stream?")
                    color: "#b5bac1"
                    font.pixelSize: 14
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }

            // Footer: dark gray strip with right-aligned buttons.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: "#2b2d31"
                radius: 6
                // Square the top corners so it looks attached to the body.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: parent.radius
                    color: parent.color
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.rightMargin: 16
                    anchors.leftMargin: 16
                    spacing: 8
                    Item { Layout.fillWidth: true }

                    // Cancel — text-only link style
                    Item {
                        Layout.preferredHeight: 38
                        Layout.preferredWidth: cancelText.implicitWidth + 24
                        Text {
                            id: cancelText
                            anchors.centerIn: parent
                            text: qsTr("Cancel")
                            color: cancelArea.containsMouse ? "white" : "#dbdee1"
                            font { pixelSize: 14; underline: cancelArea.containsMouse }
                        }
                        MouseArea {
                            id: cancelArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: leaveConfirm.close()
                        }
                    }

                    // Leave — destructive red button
                    Rectangle {
                        Layout.preferredHeight: 38
                        Layout.preferredWidth: leaveText.implicitWidth + 32
                        radius: 3
                        color: leaveArea.containsMouse ? "#c03537" : "#da373c"

                        Text {
                            id: leaveText
                            anchors.centerIn: parent
                            text: qsTr("Leave")
                            color: "white"
                            font { pixelSize: 14; bold: true }
                        }
                        MouseArea {
                            id: leaveArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: leaveConfirm.accept()
                        }
                    }
                }
            }
        }

    }
}
