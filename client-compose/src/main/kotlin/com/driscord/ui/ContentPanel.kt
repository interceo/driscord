package com.driscord.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.AppState
import com.driscord.CaptureTarget
import com.driscord.PeerInfo
import com.driscord.StreamStats

@Composable
fun ContentPanel(
    state: AppState,
    peers: List<PeerInfo>,
    streamingPeers: List<String>,
    frames: Map<String, ImageBitmap>,
    localId: String,
    watching: Boolean,
    sharing: Boolean,
    streamStats: StreamStats,
    systemAudioAvailable: Boolean,
    onListTargets: () -> List<CaptureTarget>,
    onGrabThumbnail: (CaptureTarget) -> ImageBitmap?,
    onStartSharing: (CaptureTarget, Int, Int, Boolean) -> Unit,
    onStopSharing: () -> Unit,
    onJoinStream: () -> Unit,
    onLeaveStream: () -> Unit,
    onSetStreamVolume: (Float) -> Unit,
    onStreamVolume: () -> Float,
    onGetPeerVolume: (String) -> Float,
    onSetPeerVolume: (String, Float) -> Unit,
    muted: Boolean,
    deafened: Boolean,
    selfVolume: Float,
    onSetSelfVolume: (Float) -> Unit,
    onToggleMute: () -> Unit,
    onToggleDeafen: () -> Unit,
    externalShareDialog: Boolean = false,
    onShareDialogDismiss: () -> Unit = {},
) {
    if (state != AppState.Connected) {
        Box(
            modifier = Modifier.fillMaxSize().background(ContentBg),
            contentAlignment = Alignment.Center,
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("D", color = Blurple.copy(alpha = 0.3f), fontSize = 64.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(8.dp))
                Text(
                    if (state == AppState.Connecting) "Connecting to server…" else "Connect to a server to begin",
                    color = TextMuted,
                    fontSize = 14.sp,
                )
            }
        }
        return
    }

    var showShareDialog by remember { mutableStateOf(false) }
    var focusedPeer     by remember { mutableStateOf<String?>(null) }

    // Remove focus if the peer left/stopped streaming
    LaunchedEffect(peers, streamingPeers) {
        val allIds = peers.map { it.id } + listOf(localId) + streamingPeers
        if (focusedPeer != null && focusedPeer !in allIds) focusedPeer = null
    }
    LaunchedEffect(externalShareDialog) { if (externalShareDialog) showShareDialog = true }

    Column(modifier = Modifier.fillMaxSize().background(ContentBg)) {

        // ── Top toolbar ───────────────────────────────────────────────────
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF2B2D31))
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text("General", color = TextW, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))

            if (!sharing) {
                Button(
                    onClick = { showShareDialog = true },
                    colors = ButtonDefaults.buttonColors(backgroundColor = Green),
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 12.dp),
                ) {
                    Text("Share Screen", color = Color.White, fontSize = 12.sp)
                }
            } else {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    LiveBadge()
                    Button(
                        onClick = onStopSharing,
                        colors = ButtonDefaults.buttonColors(backgroundColor = Red),
                        shape = RoundedCornerShape(4.dp),
                        modifier = Modifier.height(28.dp),
                        contentPadding = PaddingValues(horizontal = 12.dp),
                    ) {
                        Text("Stop Sharing", color = Color.White, fontSize = 12.sp)
                    }
                }
            }
        }

        Divider(color = Color(0xFF1E1F22), thickness = 1.dp)

        // ── Content ───────────────────────────────────────────────────────
        val allPeers = buildList {
            if (localId.isNotEmpty()) add(localId)
            peers.forEach { add(it.id) }
        }
        val totalItems = allPeers.size + streamingPeers.size

        if (focusedPeer != null) {
            FocusedLayout(
                focusedPeer       = focusedPeer!!,
                allPeers          = allPeers,
                streamingPeers    = streamingPeers,
                frames            = frames,
                peers             = peers,
                localId           = localId,
                watching          = watching,
                streamStats       = streamStats,
                muted             = muted,
                deafened          = deafened,
                selfVolume        = selfVolume,
                onSetSelfVolume   = onSetSelfVolume,
                onToggleMute      = onToggleMute,
                onToggleDeafen    = onToggleDeafen,
                onGetPeerVolume   = onGetPeerVolume,
                onSetPeerVolume   = onSetPeerVolume,
                onStreamVolume    = onStreamVolume,
                onSetStreamVolume = onSetStreamVolume,
                onJoinStream      = onJoinStream,
                onLeaveStream     = onLeaveStream,
                onFocus           = { focusedPeer = it },
                onUnfocus         = { focusedPeer = null },
            )
        } else {
            val cols = when {
                totalItems >= 9 -> 4
                totalItems >= 4 -> 3
                totalItems >= 2 -> 2
                else -> 1
            }
            LazyVerticalGrid(
                columns = GridCells.Fixed(cols),
                modifier = Modifier.fillMaxSize().padding(12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(allPeers, key = { it }) { peerId ->
                    UserTile(
                        peerId         = peerId,
                        label          = peerLabel(peerId, localId),
                        online         = peerId == localId || (peers.find { it.id == peerId }?.connected == true),
                        isStreaming    = streamingPeers.contains(peerId),
                        isYou          = peerId == localId,
                        muted          = if (peerId == localId) muted else false,
                        deafened       = if (peerId == localId) deafened else false,
                        onGetVolume    = if (peerId == localId) ({ selfVolume }) else ({ onGetPeerVolume(peerId) }),
                        onSetVolume    = if (peerId == localId) onSetSelfVolume else ({ v -> onSetPeerVolume(peerId, v) }),
                        onToggleMute   = if (peerId == localId) onToggleMute else null,
                        onToggleDeafen = if (peerId == localId) onToggleDeafen else null,
                        onClick        = { focusedPeer = peerId },
                    )
                }
                items(streamingPeers, key = { "stream_$it" }) { peerId ->
                    StreamTile(
                        peerId            = peerId,
                        bitmap            = frames[peerId],
                        watching          = watching,
                        stats             = streamStats,
                        streamVolume      = onStreamVolume(),
                        onSetStreamVolume = onSetStreamVolume,
                        onClick           = { focusedPeer = peerId },
                        onJoin            = onJoinStream,
                        onLeave           = onLeaveStream,
                    )
                }
            }
        }
    }

    if (showShareDialog) {
        ShareDialog(
            systemAudioAvailable = systemAudioAvailable,
            onListTargets        = onListTargets,
            onGrabThumbnail      = onGrabThumbnail,
            onDismiss            = { showShareDialog = false; onShareDialogDismiss() },
            onGo                 = { target, quality, fps, audio ->
                onStartSharing(target, quality, fps, audio)
                showShareDialog = false
                onShareDialogDismiss()
            },
        )
    }
}

// ---------------------------------------------------------------------------
// Focused layout: big tile on top + horizontal strip on bottom
// ---------------------------------------------------------------------------

@Composable
private fun FocusedLayout(
    focusedPeer: String,
    allPeers: List<String>,
    streamingPeers: List<String>,
    frames: Map<String, ImageBitmap>,
    peers: List<PeerInfo>,
    localId: String,
    watching: Boolean,
    streamStats: StreamStats,
    muted: Boolean,
    deafened: Boolean,
    selfVolume: Float,
    onSetSelfVolume: (Float) -> Unit,
    onToggleMute: () -> Unit,
    onToggleDeafen: () -> Unit,
    onGetPeerVolume: (String) -> Float,
    onSetPeerVolume: (String, Float) -> Unit,
    onStreamVolume: () -> Float,
    onSetStreamVolume: (Float) -> Unit,
    onJoinStream: () -> Unit,
    onLeaveStream: () -> Unit,
    onFocus: (String) -> Unit,
    onUnfocus: () -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxSize().padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        // ── Big tile ──────────────────────────────────────────────────────
        Box(
            modifier = Modifier.weight(1f).fillMaxWidth(),
            contentAlignment = Alignment.Center,
        ) {
            if (streamingPeers.contains(focusedPeer)) {
                StreamTile(
                    peerId            = focusedPeer,
                    bitmap            = frames[focusedPeer],
                    watching          = watching,
                    stats             = streamStats,
                    streamVolume      = onStreamVolume(),
                    onSetStreamVolume = onSetStreamVolume,
                    onClick           = onUnfocus,
                    onJoin            = onJoinStream,
                    onLeave           = onLeaveStream,
                    modifier          = Modifier.fillMaxSize(),
                )
            } else {
                val isYou = focusedPeer == localId
                val peer  = peers.find { it.id == focusedPeer }
                UserTile(
                    peerId         = focusedPeer,
                    label          = peerLabel(focusedPeer, localId),
                    online         = isYou || (peer?.connected == true),
                    isStreaming    = false,
                    isYou          = isYou,
                    muted          = if (isYou) muted else false,
                    deafened       = if (isYou) deafened else false,
                    onGetVolume    = if (isYou) ({ selfVolume }) else ({ onGetPeerVolume(focusedPeer) }),
                    onSetVolume    = if (isYou) onSetSelfVolume else ({ v -> onSetPeerVolume(focusedPeer, v) }),
                    onToggleMute   = if (isYou) onToggleMute else null,
                    onToggleDeafen = if (isYou) onToggleDeafen else null,
                    onClick        = onUnfocus,
                    modifier       = Modifier.fillMaxSize(),
                )
            }
        }

        // ── Bottom strip ─────────────────────────────────────────────────
        val otherPeers   = allPeers.filter { it != focusedPeer }
        val otherStreams  = streamingPeers.filter { it != focusedPeer }

        if (otherPeers.isNotEmpty() || otherStreams.isNotEmpty()) {
            LazyRow(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(110.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(otherPeers, key = { it }) { peerId ->
                    val isYou = peerId == localId
                    val peer  = peers.find { it.id == peerId }
                    Box(modifier = Modifier.fillMaxHeight().aspectRatio(16f / 9f)) {
                        UserTile(
                            peerId         = peerId,
                            label          = peerLabel(peerId, localId),
                            online         = isYou || (peer?.connected == true),
                            isStreaming    = streamingPeers.contains(peerId),
                            isYou          = isYou,
                            muted          = if (isYou) muted else false,
                            deafened       = if (isYou) deafened else false,
                            onGetVolume    = if (isYou) ({ selfVolume }) else ({ onGetPeerVolume(peerId) }),
                            onSetVolume    = if (isYou) onSetSelfVolume else ({ v -> onSetPeerVolume(peerId, v) }),
                            onToggleMute   = if (isYou) onToggleMute else null,
                            onToggleDeafen = if (isYou) onToggleDeafen else null,
                            onClick        = { onFocus(peerId) },
                            modifier       = Modifier.fillMaxSize(),
                        )
                    }
                }
                items(otherStreams, key = { "stream_$it" }) { peerId ->
                    Box(modifier = Modifier.fillMaxHeight().aspectRatio(16f / 9f)) {
                        StreamTile(
                            peerId            = peerId,
                            bitmap            = frames[peerId],
                            watching          = watching,
                            stats             = streamStats,
                            streamVolume      = onStreamVolume(),
                            onSetStreamVolume = onSetStreamVolume,
                            onClick           = { onFocus(peerId) },
                            onJoin            = onJoinStream,
                            onLeave           = onLeaveStream,
                            modifier          = Modifier.fillMaxSize(),
                        )
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

private fun peerLabel(peerId: String, localId: String): String = buildString {
    val s = if (peerId.length > 12) peerId.take(12) + "…" else peerId
    append(s)
    if (peerId == localId) append(" (you)")
}
