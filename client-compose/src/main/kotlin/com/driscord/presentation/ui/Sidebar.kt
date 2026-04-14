package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Button
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.CircularProgressIndicator
import androidx.compose.material.Divider
import androidx.compose.material.LinearProgressIndicator
import androidx.compose.material.LocalTextStyle
import androidx.compose.material.OutlinedTextField
import androidx.compose.material.Slider
import androidx.compose.material.SliderDefaults
import androidx.compose.material.Text
import androidx.compose.material.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.data.audio.AudioDevice
import com.driscord.domain.model.ConnectionState
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import com.driscord.presentation.ui.components.AvatarBox
import com.driscord.presentation.ui.components.IconActionButton
import com.driscord.presentation.ui.components.LiveBadge
import com.driscord.presentation.ui.components.SignalBars
import com.driscord.ui.Blurple
import com.driscord.ui.BottomBg
import com.driscord.ui.Connecting
import com.driscord.ui.DividerColor
import com.driscord.ui.FieldBg
import com.driscord.ui.Green
import com.driscord.ui.Red
import com.driscord.ui.SidebarBg
import com.driscord.ui.TextMuted
import com.driscord.ui.TextPrimary
import com.driscord.ui.VoiceBg
import org.jetbrains.compose.resources.stringResource

@Composable
fun Sidebar(
    state: AppUiState,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    onListInputDevices: () -> List<AudioDevice>,
    onListOutputDevices: () -> List<AudioDevice>,
) {
    var serverUrl by remember(state.config.serverUrl) { mutableStateOf(state.config.serverUrl) }
    var expandedPeer by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier
            .width(240.dp)
            .fillMaxHeight()
            .background(SidebarBg),
    ) {
        SidebarHeader()
        Divider(color = DividerColor, thickness = 1.dp)
        MembersList(
            state = state,
            expandedPeer = expandedPeer,
            onExpandPeer = { expandedPeer = if (expandedPeer == it) null else it },
            onIntent = onIntent,
            onGetPeerVolume = onGetPeerVolume,
            modifier = Modifier.weight(1f),
        )
        ConnectionSection(
            state = state,
            serverUrl = serverUrl,
            onServerUrlChange = { serverUrl = it },
            onIntent = onIntent,
        )
        Divider(color = DividerColor, thickness = 1.dp)
        UserBar(
            localId = state.localId.ifEmpty { "—" },
            muted = state.muted,
            deafened = state.deafened,
            sharing = state.sharing,
            connected = state.connectionState == ConnectionState.Connected,
            onToggleMute = { onIntent(AppIntent.ToggleMute) },
            onToggleDeafen = { onIntent(AppIntent.ToggleDeafen) },
            onToggleShare = {
                if (state.sharing) onIntent(AppIntent.StopSharing)
                else onIntent(AppIntent.OpenShareDialog)
            },
            onSettings = { onIntent(AppIntent.OpenSettings) },
        )
    }

    if (state.showSettings) {
        SettingsDialog(
            config = state.config,
            onDismiss = { onIntent(AppIntent.DismissSettings) },
            onSave = { onIntent(AppIntent.SaveConfig(it)) },
            onListInputDevices = onListInputDevices,
            onListOutputDevices = onListOutputDevices,
        )
    }
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

@Composable
private fun SidebarHeader() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = "DRISCORD",
            color = TextPrimary,
            fontSize = 14.sp,
            fontWeight = FontWeight.SemiBold,
            letterSpacing = 0.5.sp,
            modifier = Modifier.weight(1f),
        )
    }
}

// ---------------------------------------------------------------------------
// Members list
// ---------------------------------------------------------------------------

@Composable
private fun MembersList(
    state: AppUiState,
    expandedPeer: String?,
    onExpandPeer: (String) -> Unit,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .padding(top = 8.dp),
    ) {
        val memberCount = state.peers.size + if (state.connectionState == ConnectionState.Connected) 1 else 0
        Text(
            text = "${stringResource(Res.string.members)} — $memberCount",
            color = TextMuted,
            fontSize = 10.sp,
            letterSpacing = 0.5.sp,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )

        if (state.connectionState == ConnectionState.Connected) {
            MemberRow(
                id = state.localId,
                label = peerLabel(state.localId, state.localId),
                online = true,
                expanded = expandedPeer == state.localId,
                onClick = { onExpandPeer(state.localId) },
                isYou = true,
                muted = state.muted,
                deafened = state.deafened,
                onGetVolume = { state.outputVolume },
                onSetVolume = { v -> onIntent(AppIntent.SetOutputVolume(v)) },
                onToggleMute = { onIntent(AppIntent.ToggleMute) },
                onToggleDeafen = { onIntent(AppIntent.ToggleDeafen) },
            )
            if (expandedPeer == state.localId) {
                MemberExpanded(
                    label = stringResource(Res.string.mic_volume),
                    value = state.outputVolume,
                    level = state.inputLevel,
                    levelLabel = stringResource(Res.string.mic),
                    active = state.muted,
                    onChange = { v -> onIntent(AppIntent.SetOutputVolume(v)) },
                )
            }
        }

        state.peers.forEach { peer ->
            MemberRow(
                id = peer.id,
                label = peerLabel(peer.id, state.localId),
                online = peer.connected,
                expanded = expandedPeer == peer.id,
                onClick = { onExpandPeer(peer.id) },
                onGetVolume = { onGetPeerVolume(peer.id) },
                onSetVolume = { v -> onIntent(AppIntent.SetPeerVolume(peer.id, v)) },
            )
            if (expandedPeer == peer.id) {
                var pVol by remember(peer.id) { mutableStateOf(onGetPeerVolume(peer.id)) }
                MemberExpanded(
                    label = stringResource(Res.string.volume),
                    value = pVol,
                    level = null,
                    levelLabel = null,
                    active = false,
                    onChange = { v ->
                        pVol = v
                        onIntent(AppIntent.SetPeerVolume(peer.id, v))
                    },
                )
            }
        }

        if (state.connectionState != ConnectionState.Connected && state.peers.isEmpty()) {
            Text("  —", color = TextMuted, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 16.dp))
        }
    }
}

// ---------------------------------------------------------------------------
// Connection section
// ---------------------------------------------------------------------------

@Composable
private fun ConnectionSection(
    state: AppUiState,
    serverUrl: String,
    onServerUrlChange: (String) -> Unit,
    onIntent: (AppIntent) -> Unit,
) {
    when (state.connectionState) {
        ConnectionState.Disconnected -> {
            Column(modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp)) {
                Text(stringResource(Res.string.server), color = TextMuted, fontSize = 11.sp, letterSpacing = 0.3.sp)
                Spacer(Modifier.height(4.dp))
                OutlinedTextField(
                    value = serverUrl,
                    onValueChange = onServerUrlChange,
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = TextFieldDefaults.outlinedTextFieldColors(
                        textColor = TextPrimary,
                        unfocusedBorderColor = FieldBg,
                        focusedBorderColor = Blurple,
                        backgroundColor = BottomBg,
                        cursorColor = Blurple,
                    ),
                    textStyle = LocalTextStyle.current.copy(fontSize = 12.sp),
                )
                Spacer(Modifier.height(8.dp))
                Button(
                    onClick = { onIntent(AppIntent.Connect(serverUrl)) },
                    modifier = Modifier.fillMaxWidth().height(32.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(0.dp),
                ) {
                    Text(stringResource(Res.string.connect), color = Color.White, fontSize = 13.sp)
                }
            }
        }

        ConnectionState.Connecting -> {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(VoiceBg)
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CircularProgressIndicator(modifier = Modifier.size(14.dp), strokeWidth = 2.dp, color = Connecting)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(Res.string.connecting), color = Connecting, fontSize = 13.sp)
            }
        }

        ConnectionState.Connected -> {
            ConnectedStatus(state = state, onIntent = onIntent)
        }
    }
}

@Composable
private fun ConnectedStatus(state: AppUiState, onIntent: (AppIntent) -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(VoiceBg)
            .padding(horizontal = 12.dp, vertical = 8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SignalBars(modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(6.dp))
            Text(
                text = stringResource(Res.string.voice_connected),
                color = Green,
                fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f),
            )
            Box(
                modifier = Modifier
                    .size(20.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(FieldBg)
                    .clickable { onIntent(AppIntent.Disconnect) },
                contentAlignment = Alignment.Center,
            ) {
                Text("✕", color = Red, fontSize = 10.sp)
            }
        }
        Spacer(Modifier.height(2.dp))
        val shortUrl = state.config.serverUrl.removePrefix("ws://").removePrefix("wss://")
        Text(
            text = "${state.peers.size + 1} connected  ·  $shortUrl",
            color = TextMuted,
            fontSize = 11.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (state.sharing) {
            Spacer(Modifier.height(4.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                LiveBadge()
                Spacer(Modifier.width(5.dp))
                Text(
                    text = state.shareTargetName.ifEmpty { stringResource(Res.string.screen) },
                    color = Green,
                    fontSize = 11.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

// ---------------------------------------------------------------------------
// User bar (bottom of sidebar)
// ---------------------------------------------------------------------------

@Composable
private fun UserBar(
    localId: String,
    muted: Boolean,
    deafened: Boolean,
    sharing: Boolean,
    connected: Boolean,
    onToggleMute: () -> Unit,
    onToggleDeafen: () -> Unit,
    onToggleShare: () -> Unit,
    onSettings: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(BottomBg)
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AvatarBox(peerId = localId, size = 28, fontSize = 12)
        Spacer(Modifier.width(8.dp))
        Column(modifier = Modifier.weight(1f)) {
            val short = if (localId.length > 12) localId.take(12) + "…" else localId
            Text(
                text = short,
                color = TextPrimary,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                if (connected) stringResource(Res.string.connected) else stringResource(Res.string.offline),
                color = TextMuted,
                fontSize = 10.sp,
            )
        }
        if (connected) {
            IconActionButton(if (muted) "M!" else "M", muted, Red, onToggleMute)
            Spacer(Modifier.width(2.dp))
            IconActionButton(if (deafened) "H!" else "H", deafened, Red, onToggleDeafen)
            Spacer(Modifier.width(2.dp))
            IconActionButton(if (sharing) "◼" else "◻", sharing, Green, onToggleShare)
            Spacer(Modifier.width(2.dp))
        }
        IconActionButton("⚙", false, Red, onSettings)
    }
}

// ---------------------------------------------------------------------------
// Member row — one entry in the members list
// ---------------------------------------------------------------------------

@Composable
private fun MemberRow(
    id: String,
    label: String,
    online: Boolean,
    expanded: Boolean,
    onClick: () -> Unit,
    isYou: Boolean = false,
    muted: Boolean = false,
    deafened: Boolean = false,
    onGetVolume: () -> Float = { 1f },
    onSetVolume: (Float) -> Unit = {},
    onToggleMute: (() -> Unit)? = null,
    onToggleDeafen: (() -> Unit)? = null,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AvatarBox(peerId = id, size = 24, fontSize = 11)
        Spacer(Modifier.width(8.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = label,
                color = if (online) TextPrimary else TextMuted,
                fontSize = 13.sp,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (isYou) {
            if (muted) Text("M", color = Red, fontSize = 10.sp)
            if (deafened) Text("H", color = Red, fontSize = 10.sp)
        }
    }
}

// ---------------------------------------------------------------------------
// Member expanded — volume slider shown below a member row
// ---------------------------------------------------------------------------

@Composable
private fun MemberExpanded(
    label: String,
    value: Float,
    level: Float?,
    levelLabel: String?,
    active: Boolean,
    onChange: (Float) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(BottomBg)
            .padding(horizontal = 16.dp, vertical = 6.dp),
    ) {
        Text(label, color = TextMuted, fontSize = 10.sp)
        Slider(
            value = value,
            onValueChange = onChange,
            valueRange = 0f..2f,
            colors = SliderDefaults.colors(
                thumbColor = if (active) Red else Blurple,
                activeTrackColor = if (active) Red else Blurple,
            ),
            modifier = Modifier.fillMaxWidth().height(28.dp),
        )
        if (level != null && levelLabel != null) {
            Spacer(Modifier.height(2.dp))
            Text(levelLabel, color = TextMuted, fontSize = 10.sp)
            LinearProgressIndicator(
                progress = level.coerceIn(0f, 1f),
                modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                color = Green,
                backgroundColor = FieldBg,
            )
        }
    }
}
