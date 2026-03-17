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
    val state           by app.state.collectAsState()
    val peers           by app.peers.collectAsState()
    val streamingPeers  by app.streamingPeers.collectAsState()
    val frames          by app.frames.collectAsState()
    val muted           by app.muted.collectAsState()
    val deafened        by app.deafened.collectAsState()
    val volume          by app.volume.collectAsState()
    val inputLevel      by app.inputLevel.collectAsState()
    val outputLevel     by app.outputLevel.collectAsState()
    val localId         by app.localId.collectAsState()
    val watching        by app.watching.collectAsState()
    val sharing         by app.sharing.collectAsState()
    val shareTargetName by app.shareTargetName.collectAsState()
    val streamStats     by app.streamStats.collectAsState()
    val config          by app.currentConfig.collectAsState()

    var showShareDialog by remember { mutableStateOf(false) }

    Row(modifier = Modifier.fillMaxSize().background(Color(0xFF313338))) {

        // ── Left sidebar ────────────────────────────────────────────────
        Sidebar(
            state            = state,
            localId          = localId,
            peers            = peers,
            muted            = muted,
            deafened         = deafened,
            sharing          = sharing,
            shareTargetName  = shareTargetName,
            volume           = volume,
            inputLevel       = inputLevel,
            outputLevel      = outputLevel,
            config           = config,
            initialServerUrl = config.serverUrl,
            onConnect        = { app.connect(it) },
            onDisconnect     = { app.disconnect() },
            onToggleMute     = { app.toggleMute() },
            onToggleDeafen   = { app.toggleDeafen() },
            onSetVolume      = { app.setVolume(it) },
            onSetPeerVolume  = { id, v -> app.setPeerVolume(id, v) },
            onGetPeerVolume  = { app.peerVolume(it) },
            onStartShare     = { showShareDialog = true },
            onStopShare      = { app.stopSharing() },
            onSaveConfig     = { app.saveConfig(it) },
        )

        // ── Main content area ───────────────────────────────────────────
        Box(modifier = Modifier.fillMaxSize()) {
            ContentPanel(
                state                = state,
                peers                = peers,
                streamingPeers       = streamingPeers,
                frames               = frames,
                localId              = localId,
                watching             = watching,
                sharing              = sharing,
                streamStats          = streamStats,
                systemAudioAvailable = app.systemAudioAvailable,
                onListTargets        = { app.listCaptureTargets() },
                onGrabThumbnail      = { app.grabThumbnail(it) },
                onStartSharing       = { t, q, fps, audio -> app.startSharing(t, q, fps, audio) },
                onStopSharing        = { app.stopSharing() },
                onJoinStream         = { app.joinStream() },
                onLeaveStream        = { app.leaveStream() },
                onSetStreamVolume    = { app.setStreamVolume(it) },
                onStreamVolume       = { app.streamVolume() },
                externalShareDialog  = showShareDialog,
                onShareDialogDismiss = { showShareDialog = false },
            )
        }
    }
}
