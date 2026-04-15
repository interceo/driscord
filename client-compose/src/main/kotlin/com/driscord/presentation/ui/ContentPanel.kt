package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items as lazyItems
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.Button
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.Divider
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.domain.model.CaptureTarget
import com.driscord.domain.model.ConnectionState
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import com.driscord.presentation.ui.components.LiveBadge
import com.driscord.presentation.ui.components.UserTile
import com.driscord.ui.Blurple
import com.driscord.ui.ContentBg
import com.driscord.ui.DividerColor
import com.driscord.ui.Green
import com.driscord.ui.Red
import com.driscord.ui.SidebarBg
import com.driscord.ui.TextMuted
import com.driscord.ui.TextPrimary
import org.jetbrains.compose.resources.stringResource

@Composable
fun ContentPanel(
    state: AppUiState,
    frames: Map<String, ImageBitmap>,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    onStreamVolume: () -> Float,
    onListTargets: () -> List<CaptureTarget>,
    onGrabThumbnail: (CaptureTarget) -> ImageBitmap?,
    modifier: Modifier = Modifier,
) {
    if (state.connectionState != ConnectionState.Connected) {
        DisconnectedPlaceholder(
            connecting = state.connectionState == ConnectionState.Connecting,
            modifier = modifier,
        )
        return
    }

    var focusedPeer by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(state.peers, state.streamingPeers) {
        val allIds = state.peers.map { it.id } + state.localId + state.streamingPeers
        if (focusedPeer != null && focusedPeer !in allIds) focusedPeer = null
    }

    val allPeers = buildList {
        if (state.localId.isNotEmpty()) add(state.localId)
        state.peers.forEach { add(it.id) }
    }

    Column(modifier = modifier.fillMaxSize().background(ContentBg)) {
        Toolbar(state = state, onIntent = onIntent)
        Divider(color = DividerColor, thickness = 1.dp)

        if (focusedPeer != null) {
            FocusedLayout(
                focusedPeer = focusedPeer!!,
                state = state,
                frames = frames,
                allPeers = allPeers,
                onIntent = onIntent,
                onGetPeerVolume = onGetPeerVolume,
                onStreamVolume = onStreamVolume,
                onFocus = { focusedPeer = it },
                onUnfocus = { focusedPeer = null },
            )
        } else {
            PeerGrid(
                allPeers = allPeers,
                state = state,
                frames = frames,
                onIntent = onIntent,
                onGetPeerVolume = onGetPeerVolume,
                onStreamVolume = onStreamVolume,
                onFocusPeer = { focusedPeer = it },
            )
        }
    }

    if (state.showShareDialog) {
        ShareDialog(
            systemAudioAvailable = state.systemAudioAvailable,
            onListTargets = onListTargets,
            onGrabThumbnail = onGrabThumbnail,
            onDismiss = { onIntent(AppIntent.DismissShareDialog) },
            onGo = { target, quality, fps, audio ->
                onIntent(AppIntent.StartSharing(target, quality, fps, audio))
            },
        )
    }
}

// ---------------------------------------------------------------------------
// Disconnected placeholder
// ---------------------------------------------------------------------------

@Composable
private fun DisconnectedPlaceholder(connecting: Boolean, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.fillMaxSize().background(ContentBg),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text("D", color = Blurple.copy(alpha = 0.3f), fontSize = 64.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(8.dp))
            Text(
                text = if (connecting) stringResource(Res.string.connecting_to_server)
                       else stringResource(Res.string.connect_to_begin),
                color = TextMuted,
                fontSize = 14.sp,
            )
        }
    }
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

@Composable
private fun Toolbar(state: AppUiState, onIntent: (AppIntent) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(SidebarBg)
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(
            text = stringResource(Res.string.general),
            color = TextPrimary,
            fontSize = 14.sp,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.weight(1f),
        )
        if (state.sharing) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                LiveBadge()
                Button(
                    onClick = { onIntent(AppIntent.StopSharing) },
                    colors = ButtonDefaults.buttonColors(backgroundColor = Red),
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 12.dp),
                ) {
                    Text(stringResource(Res.string.stop_sharing), color = Color.White, fontSize = 12.sp)
                }
            }
        } else {
            Button(
                onClick = { onIntent(AppIntent.OpenShareDialog) },
                colors = ButtonDefaults.buttonColors(backgroundColor = Green),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.height(28.dp),
                contentPadding = PaddingValues(horizontal = 12.dp),
            ) {
                Text(stringResource(Res.string.share_screen), color = Color.White, fontSize = 12.sp)
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Peer grid
// ---------------------------------------------------------------------------

@Composable
private fun PeerGrid(
    allPeers: List<String>,
    state: AppUiState,
    frames: Map<String, ImageBitmap>,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    onStreamVolume: () -> Float,
    onFocusPeer: (String) -> Unit,
) {
    val totalItems = allPeers.size + state.streamingPeers.size
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
            PeerTile(
                peerId = peerId,
                state = state,
                onIntent = onIntent,
                onGetPeerVolume = onGetPeerVolume,
                onClick = { onFocusPeer(peerId) },
                modifier = Modifier.fillMaxWidth().aspectRatio(16f / 9f),
            )
        }
        items(state.streamingPeers, key = { "stream_$it" }) { peerId ->
            StreamTile(
                peerId = peerId,
                bitmap = frames[peerId],
                watching = state.watching,
                stats = state.streamStats,
                streamVolume = onStreamVolume(),
                onSetStreamVolume = { v -> onIntent(AppIntent.SetStreamVolume(peerId, v)) },
                onClick = { onFocusPeer(peerId) },
                onJoin = { onIntent(AppIntent.JoinStream) },
                onLeave = { onIntent(AppIntent.LeaveStream) },
                modifier = Modifier.fillMaxWidth().aspectRatio(16f / 9f),
            )
        }
    }
}

// ---------------------------------------------------------------------------
// Peer tile — bridges AppUiState to UserTile params, used by grid and FocusedLayout
// ---------------------------------------------------------------------------

@Composable
internal fun PeerTile(
    peerId: String,
    state: AppUiState,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val isYou = peerId == state.localId
    UserTile(
        peerId = peerId,
        label = peerLabel(peerId, state.localId),
        online = isYou || state.peers.any { it.id == peerId && it.connected },
        isStreaming = peerId in state.streamingPeers,
        isYou = isYou,
        muted = isYou && state.muted,
        deafened = isYou && state.deafened,
        onGetVolume = if (isYou) ({ state.outputVolume }) else ({ onGetPeerVolume(peerId) }),
        onSetVolume = if (isYou) { v -> onIntent(AppIntent.SetOutputVolume(v)) }
                      else { v -> onIntent(AppIntent.SetPeerVolume(peerId, v)) },
        onToggleMute = if (isYou) ({ onIntent(AppIntent.ToggleMute) }) else null,
        onToggleDeafen = if (isYou) ({ onIntent(AppIntent.ToggleDeafen) }) else null,
        onClick = onClick,
        modifier = modifier,
    )
}

// ---------------------------------------------------------------------------
// Focused layout — one peer large, others in a strip below
// ---------------------------------------------------------------------------

@Composable
private fun FocusedLayout(
    focusedPeer: String,
    state: AppUiState,
    frames: Map<String, ImageBitmap>,
    allPeers: List<String>,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    onStreamVolume: () -> Float,
    onFocus: (String?) -> Unit,
    onUnfocus: () -> Unit,
) {
    Column(modifier = Modifier.fillMaxSize()) {
        val isStream = focusedPeer in state.streamingPeers
        Box(modifier = Modifier.weight(1f).fillMaxWidth()) {
            if (isStream) {
                StreamTile(
                    peerId = focusedPeer,
                    bitmap = frames[focusedPeer],
                    watching = state.watching,
                    stats = state.streamStats,
                    streamVolume = onStreamVolume(),
                    onSetStreamVolume = { v -> onIntent(AppIntent.SetStreamVolume(focusedPeer, v)) },
                    onClick = onUnfocus,
                    onJoin = { onIntent(AppIntent.JoinStream) },
                    onLeave = { onIntent(AppIntent.LeaveStream) },
                    modifier = Modifier.fillMaxSize(),
                )
            } else {
                PeerTile(
                    peerId = focusedPeer,
                    state = state,
                    onIntent = onIntent,
                    onGetPeerVolume = onGetPeerVolume,
                    onClick = onUnfocus,
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }

        val others = (allPeers + state.streamingPeers).distinct().filter { it != focusedPeer }
        if (others.isNotEmpty()) {
            Divider(color = DividerColor, thickness = 1.dp)
            LazyRow(
                modifier = Modifier.fillMaxWidth().height(120.dp).padding(8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                lazyItems(others, key = { it }) { peerId ->
                    val isStr = peerId in state.streamingPeers
                    if (isStr) {
                        StreamTile(
                            peerId = peerId,
                            bitmap = frames[peerId],
                            watching = state.watching,
                            stats = state.streamStats,
                            streamVolume = onStreamVolume(),
                            onSetStreamVolume = { v -> onIntent(AppIntent.SetStreamVolume(peerId, v)) },
                            onClick = { onFocus(peerId) },
                            onJoin = { onIntent(AppIntent.JoinStream) },
                            onLeave = { onIntent(AppIntent.LeaveStream) },
                            modifier = Modifier.fillMaxHeight().aspectRatio(16f / 9f),
                        )
                    } else {
                        PeerTile(
                            peerId = peerId,
                            state = state,
                            onIntent = onIntent,
                            onGetPeerVolume = onGetPeerVolume,
                            onClick = { onFocus(peerId) },
                            modifier = Modifier.fillMaxHeight().aspectRatio(16f / 9f),
                        )
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Shared utility — used across ui package
// ---------------------------------------------------------------------------

@Composable
internal fun peerLabel(peerId: String, localId: String): String = buildString {
    append(if (peerId.length > 14) peerId.take(14) + "…" else peerId)
    if (peerId == localId) append(" (${stringResource(Res.string.you)})")
}
