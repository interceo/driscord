package com.driscord.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.driscord.DriscordApp

@Composable
fun MainScreen(app: DriscordApp) {
    val state by app.state.collectAsState()
    val peers by app.peers.collectAsState()
    val streamingPeers by app.streamingPeers.collectAsState()
    val frames by app.frames.collectAsState()
    val muted by app.muted.collectAsState()
    val deafened by app.deafened.collectAsState()
    val volume by app.volume.collectAsState()
    val inputLevel by app.inputLevel.collectAsState()
    val outputLevel by app.outputLevel.collectAsState()
    val localId by app.localId.collectAsState()
    val watching by app.watching.collectAsState()
    val sharing by app.sharing.collectAsState()
    val streamStats by app.streamStats.collectAsState()

    Row(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF2C2F33))
    ) {
        // Left sidebar
        Box(
            modifier = Modifier
                .width(240.dp)
                .fillMaxHeight()
                .background(Color(0xFF23272A))
        ) {
            Sidebar(
                state = state,
                localId = localId,
                peers = peers,
                muted = muted,
                deafened = deafened,
                volume = volume,
                inputLevel = inputLevel,
                outputLevel = outputLevel,
                onConnect = { url -> app.connect(url) },
                onDisconnect = { app.disconnect() },
                onToggleMute = { app.toggleMute() },
                onToggleDeafen = { app.toggleDeafen() },
                onSetVolume = { app.setVolume(it) },
                onSetPeerVolume = { id, vol -> app.setPeerVolume(id, vol) },
                onGetPeerVolume = { app.peerVolume(it) },
            )
        }

        // Main content
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(12.dp)
        ) {
            ContentPanel(
                state = state,
                peers = peers,
                streamingPeers = streamingPeers,
                frames = frames,
                localId = localId,
                watching = watching,
                sharing = sharing,
                streamStats = streamStats,
                systemAudioAvailable = app.systemAudioAvailable,
                onListTargets = { app.listCaptureTargets() },
                onGrabThumbnail = { target -> app.grabThumbnail(target) },
                onStartSharing = { target, quality, fps, audio ->
                    app.startSharing(target, quality, fps, audio)
                },
                onStopSharing = { app.stopSharing() },
                onJoinStream = { app.joinStream() },
                onLeaveStream = { app.leaveStream() },
                onSetStreamVolume = { app.setStreamVolume(it) },
                onStreamVolume = { app.streamVolume() },
            )
        }
    }
}
