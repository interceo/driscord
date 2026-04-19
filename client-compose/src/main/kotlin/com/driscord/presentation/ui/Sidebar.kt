package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.data.audio.AudioDevice
import com.driscord.domain.model.Channel
import com.driscord.domain.model.ChannelKind
import com.driscord.domain.model.ConnectionState
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import com.driscord.presentation.ui.components.AvatarBox
import com.driscord.presentation.ui.components.IconActionButton
import com.driscord.presentation.ui.components.LiveBadge
import com.driscord.presentation.ui.components.SignalBars
import com.driscord.ui.*
import org.jetbrains.compose.resources.stringResource

@Composable
fun Sidebar(
    state: AppUiState,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
    onListInputDevices: () -> List<AudioDevice>,
    onListOutputDevices: () -> List<AudioDevice>,
) {
    var expandedPeer by remember { mutableStateOf<String?>(null) }
    val selectedServer = state.servers.find { it.id == state.selectedServerId }

    Column(
        modifier = Modifier
            .width(200.dp)
            .fillMaxHeight()
            .background(SidebarBg),
    ) {
        // Header — server name or app name
        SidebarHeader(
            serverName = selectedServer?.name ?: "DRISCORD",
            onInvite = if (selectedServer != null) {
                { onIntent(AppIntent.CreateInvite(selectedServer.id)) }
            } else null,
        )
        Divider(color = DividerColor, thickness = 1.dp)

        // Channel list (top section) — shown whenever a server is selected
        if (selectedServer != null) {
            ChannelList(
                channels = state.channels,
                selectedChannelId = state.selectedChannelId,
                onIntent = onIntent,
                modifier = Modifier.weight(1f),
            )
        } else {
            Spacer(modifier = Modifier.weight(1f))
        }

        // Voice connected status
        if (state.connectionState != ConnectionState.Disconnected) {
            Divider(color = DividerColor, thickness = 1.dp)
            ConnectionStatus(state = state, onIntent = onIntent)
        }

        // Members list
        if (state.connectionState == ConnectionState.Connected) {
            Divider(color = DividerColor, thickness = 1.dp)
            MembersList(
                state = state,
                expandedPeer = expandedPeer,
                onExpandPeer = { expandedPeer = if (expandedPeer == it) null else it },
                onIntent = onIntent,
                onGetPeerVolume = onGetPeerVolume,
                modifier = Modifier.heightIn(max = 200.dp),
            )
        }

        Divider(color = DividerColor, thickness = 1.dp)
        UserBar(
            username = state.currentUsername.ifEmpty { "—" },
            localId = state.localId,
            muted = state.muted,
            deafened = state.deafened,
            connected = state.connectionState == ConnectionState.Connected,
            onToggleMute = { onIntent(AppIntent.ToggleMute) },
            onToggleDeafen = { onIntent(AppIntent.ToggleDeafen) },
            onSettings = { onIntent(AppIntent.OpenSettings) },
            onLogout = { onIntent(AppIntent.Logout) },
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

    if (state.showCreateServerDialog) {
        CreateServerDialog(
            onDismiss = { onIntent(AppIntent.DismissCreateServerDialog) },
            onCreate = { name -> onIntent(AppIntent.CreateServer(name)) },
        )
    }

    if (state.showCreateChannelDialog) {
        CreateChannelDialog(
            defaultKind = state.createChannelDefaultKind,
            onDismiss = { onIntent(AppIntent.DismissCreateChannelDialog) },
            onCreate = { name, kind -> onIntent(AppIntent.CreateChannel(name, kind)) },
        )
    }

    if (state.inviteDialogCode != null) {
        InviteCodeDialog(
            code = state.inviteDialogCode,
            serverName = state.inviteDialogServerName,
            onDismiss = { onIntent(AppIntent.DismissInviteDialog) },
        )
    } else if (state.showJoinByInviteDialog) {
        JoinByInviteDialog(
            onDismiss = { onIntent(AppIntent.DismissInviteDialog) },
            onJoin = { code -> onIntent(AppIntent.AcceptInvite(code)) },
        )
    }
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

@Composable
private fun SidebarHeader(serverName: String, onInvite: (() -> Unit)? = null) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = serverName,
            color = TextPrimary,
            fontSize = 13.sp,
            fontWeight = FontWeight.SemiBold,
            letterSpacing = 0.4.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
        if (onInvite != null) {
            Box(
                modifier = Modifier
                    .size(20.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(FieldBg)
                    .clickable(onClick = onInvite),
                contentAlignment = Alignment.Center,
            ) {
                Text("+", color = TextMuted, fontSize = 14.sp, lineHeight = 14.sp)
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Channel list
// ---------------------------------------------------------------------------

@Composable
private fun ChannelList(
    channels: List<Channel>,
    selectedChannelId: Int?,
    onIntent: (AppIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val voiceChannels = channels.filter { it.kind == ChannelKind.voice }
    val textChannels = channels.filter { it.kind == ChannelKind.text }

    Column(
        modifier = modifier.verticalScroll(rememberScrollState()).padding(top = 6.dp),
    ) {
        ChannelSectionLabel(
            text = stringResource(Res.string.voice_channels),
            onAdd = { onIntent(AppIntent.OpenCreateChannelDialog(ChannelKind.voice)) },
        )
        voiceChannels.forEach { ch ->
            ChannelRow(
                name = ch.name,
                prefix = "♪",
                selected = ch.id == selectedChannelId,
                enabled = true,
                onClick = { onIntent(AppIntent.SelectChannel(ch.id)) },
            )
        }

        Spacer(Modifier.height(4.dp))
        ChannelSectionLabel(
            text = stringResource(Res.string.text_channels),
            onAdd = { onIntent(AppIntent.OpenCreateChannelDialog(ChannelKind.text)) },
        )
        textChannels.forEach { ch ->
            ChannelRow(
                name = ch.name,
                prefix = "#",
                selected = false,
                enabled = false,
                onClick = {},
            )
        }
        Spacer(Modifier.height(6.dp))
    }
}

@Composable
private fun ChannelSectionLabel(text: String, onAdd: (() -> Unit)? = null) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(start = 12.dp, end = 6.dp, top = 4.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = text,
            color = TextMuted,
            fontSize = 10.sp,
            letterSpacing = 0.5.sp,
            modifier = Modifier.weight(1f),
        )
        if (onAdd != null) {
            Box(
                modifier = Modifier
                    .size(16.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .clickable(onClick = onAdd),
                contentAlignment = Alignment.Center,
            ) {
                Text("+", color = TextMuted, fontSize = 14.sp, lineHeight = 14.sp)
            }
        }
    }
}

@Composable
private fun ChannelRow(
    name: String,
    prefix: String,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    val bg = if (selected) Blurple.copy(alpha = 0.25f) else Color.Transparent
    val textColor = when {
        selected -> TextPrimary
        enabled -> TextPrimary.copy(alpha = 0.85f)
        else -> TextMuted
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(4.dp))
            .background(bg)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(prefix, color = TextMuted, fontSize = 12.sp, modifier = Modifier.width(18.dp))
        Text(
            text = name,
            color = textColor,
            fontSize = 13.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

// ---------------------------------------------------------------------------
// Voice connection status (compact)
// ---------------------------------------------------------------------------

@Composable
private fun ConnectionStatus(state: AppUiState, onIntent: (AppIntent) -> Unit) {
    val channelName = state.channels.find { it.id == state.selectedChannelId }?.name
    val serverName = state.servers.find { it.id == state.selectedServerId }?.name
    val subtitle = when {
        serverName != null && channelName != null -> "$serverName / $channelName"
        channelName != null -> channelName
        else -> null
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(VoiceBg)
            .padding(horizontal = 10.dp, vertical = 8.dp),
    ) {
        when (state.connectionState) {
            ConnectionState.Connecting -> {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(modifier = Modifier.size(12.dp), strokeWidth = 2.dp, color = Connecting)
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(Res.string.connecting), color = Connecting, fontSize = 12.sp)
                }
            }
            ConnectionState.Connected -> {
                // Header row: signal + title + disconnect
                Row(verticalAlignment = Alignment.CenterVertically) {
                    SignalBars(modifier = Modifier.size(14.dp))
                    Spacer(Modifier.width(6.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = stringResource(Res.string.voice_connected),
                            color = Green,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                        )
                        if (subtitle != null) {
                            Text(
                                text = subtitle,
                                color = TextMuted,
                                fontSize = 10.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                    }
                    Box(
                        modifier = Modifier
                            .size(18.dp)
                            .clip(RoundedCornerShape(3.dp))
                            .background(FieldBg)
                            .clickable { onIntent(AppIntent.Disconnect) },
                        contentAlignment = Alignment.Center,
                    ) { Text("✕", color = Red, fontSize = 9.sp) }
                }
                // Share controls row
                Spacer(Modifier.height(6.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ShareButton(
                        label = stringResource(Res.string.share_screen),
                        active = state.sharing,
                        modifier = Modifier.weight(1f),
                        onClick = {
                            if (state.sharing) onIntent(AppIntent.StopSharing)
                            else onIntent(AppIntent.OpenShareDialog)
                        },
                    )
                }
                if (state.sharing) {
                    Spacer(Modifier.height(4.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        LiveBadge()
                        Spacer(Modifier.width(5.dp))
                        Text(
                            text = state.shareTargetName.ifEmpty { stringResource(Res.string.screen) },
                            color = Green,
                            fontSize = 10.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            }
            else -> {}
        }
    }
}

@Composable
private fun ShareButton(label: String, active: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Row(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(if (active) Blurple else FieldBg)
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        Text("◻", color = if (active) Color.White else TextMuted, fontSize = 10.sp)
        Spacer(Modifier.width(4.dp))
        Text(label, color = if (active) Color.White else TextMuted, fontSize = 11.sp, maxLines = 1)
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
            .padding(top = 6.dp),
    ) {
        val memberCount = state.peers.size + 1
        Text(
            text = "${stringResource(Res.string.members)} — $memberCount",
            color = TextMuted,
            fontSize = 10.sp,
            letterSpacing = 0.5.sp,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 3.dp),
        )

        val localLabel = if (state.currentUsername.isNotEmpty())
            "${state.currentUsername} (${stringResource(Res.string.you)})"
        else
            peerLabel(state.localId, state.localId)
        MemberRow(
            id = state.localId,
            label = localLabel,
            online = true,
            expanded = expandedPeer == state.localId,
            onClick = { onExpandPeer(state.localId) },
            isYou = true,
            muted = state.muted,
            deafened = state.deafened,
            onGetVolume = { state.outputVolume },
            onSetVolume = { v -> onIntent(AppIntent.SetOutputVolume(v)) },
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

        state.peers.forEach { peer ->
            MemberRow(
                id = peer.id,
                label = peer.username.ifEmpty { peerLabel(peer.id, state.localId) },
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
        Spacer(Modifier.height(4.dp))
    }
}

// ---------------------------------------------------------------------------
// User bar (bottom of sidebar)
// ---------------------------------------------------------------------------

@Composable
private fun UserBar(
    username: String,
    localId: String,
    muted: Boolean,
    deafened: Boolean,
    connected: Boolean,
    onToggleMute: () -> Unit,
    onToggleDeafen: () -> Unit,
    onSettings: () -> Unit,
    onLogout: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(BottomBg)
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Avatar with online indicator
        Box {
            AvatarBox(peerId = localId.ifEmpty { username }, size = 28, fontSize = 12)
            if (connected) {
                Box(
                    modifier = Modifier
                        .size(9.dp)
                        .background(Green, CircleShape)
                        .align(Alignment.BottomEnd),
                )
            }
        }
        Spacer(Modifier.width(7.dp))
        // Username — takes all remaining space before buttons
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = username,
                color = TextPrimary,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = if (connected) stringResource(Res.string.connected) else stringResource(Res.string.offline),
                color = if (connected) Green else TextMuted,
                fontSize = 10.sp,
                maxLines = 1,
            )
        }
        // Fixed-width action area: at most 4 × 22dp + 3 × 2dp = 94dp
        if (connected) {
            UserBarButton(if (muted) "M!" else "M", muted, Red, onToggleMute)
            Spacer(Modifier.width(2.dp))
            UserBarButton(if (deafened) "H!" else "H", deafened, Red, onToggleDeafen)
            Spacer(Modifier.width(2.dp))
        }
        UserBarButton("⚙", false, TextMuted, onSettings)
        Spacer(Modifier.width(2.dp))
        UserBarButton("↪", false, TextMuted, onLogout)
    }
}

@Composable
private fun UserBarButton(label: String, active: Boolean, color: Color, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(22.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(if (active) color.copy(alpha = 0.2f) else Color.Transparent)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (active) color else TextMuted, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
}

// ---------------------------------------------------------------------------
// Member row
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
        AvatarBox(peerId = id, size = 22, fontSize = 10)
        Spacer(Modifier.width(7.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = label,
                color = if (online) TextPrimary else TextMuted,
                fontSize = 12.sp,
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
// Member expanded (volume slider)
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
            .padding(horizontal = 14.dp, vertical = 5.dp),
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
            modifier = Modifier.fillMaxWidth().height(26.dp),
        )
        if (level != null && levelLabel != null) {
            Spacer(Modifier.height(2.dp))
            Text(levelLabel, color = TextMuted, fontSize = 10.sp)
            LinearProgressIndicator(
                progress = level.coerceIn(0f, 1f),
                modifier = Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp)),
                color = Green,
                backgroundColor = FieldBg,
            )
        }
    }
}

// ---------------------------------------------------------------------------
// Create channel dialog
// ---------------------------------------------------------------------------

@Composable
private fun CreateChannelDialog(
    defaultKind: ChannelKind = ChannelKind.voice,
    onDismiss: () -> Unit,
    onCreate: (String, ChannelKind) -> Unit,
) {
    var name by remember { mutableStateOf("") }
    var kind by remember { mutableStateOf(defaultKind) }

    AlertDialog(
        onDismissRequest = onDismiss,
        backgroundColor = SidebarBg,
        title = {
            Text(
                stringResource(Res.string.create_channel),
                color = TextPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold,
            )
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    singleLine = true,
                    label = { Text(stringResource(Res.string.channel_name), color = TextMuted, fontSize = 12.sp) },
                    colors = TextFieldDefaults.outlinedTextFieldColors(
                        textColor = TextPrimary,
                        unfocusedBorderColor = FieldBg,
                        focusedBorderColor = Blurple,
                        backgroundColor = BottomBg,
                        cursorColor = Blurple,
                    ),
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    KindChip(
                        label = stringResource(Res.string.voice),
                        selected = kind == ChannelKind.voice,
                        onClick = { kind = ChannelKind.voice },
                        modifier = Modifier.weight(1f),
                    )
                    KindChip(
                        label = stringResource(Res.string.text),
                        selected = kind == ChannelKind.text,
                        onClick = { kind = ChannelKind.text },
                        modifier = Modifier.weight(1f),
                    )
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { if (name.isNotBlank()) onCreate(name, kind) }, enabled = name.isNotBlank()) {
                Text(stringResource(Res.string.create_channel), color = Blurple)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.cancel), color = TextMuted)
            }
        },
    )
}

@Composable
private fun KindChip(label: String, selected: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(if (selected) Blurple else FieldBg)
            .clickable(onClick = onClick)
            .padding(vertical = 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (selected) Color.White else TextMuted, fontSize = 12.sp)
    }
}

// ---------------------------------------------------------------------------
// Create server dialog
// ---------------------------------------------------------------------------

@Composable
private fun CreateServerDialog(onDismiss: () -> Unit, onCreate: (String) -> Unit) {
    var name by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        backgroundColor = SidebarBg,
        title = { Text(stringResource(Res.string.create_server), color = TextPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold) },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                singleLine = true,
                label = { Text(stringResource(Res.string.server_name), color = TextMuted, fontSize = 12.sp) },
                colors = TextFieldDefaults.outlinedTextFieldColors(
                    textColor = TextPrimary,
                    unfocusedBorderColor = FieldBg,
                    focusedBorderColor = Blurple,
                    backgroundColor = BottomBg,
                    cursorColor = Blurple,
                ),
                modifier = Modifier.fillMaxWidth(),
            )
        },
        confirmButton = {
            TextButton(onClick = { if (name.isNotBlank()) onCreate(name) }, enabled = name.isNotBlank()) {
                Text(stringResource(Res.string.create_server), color = Blurple)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.cancel), color = TextMuted)
            }
        },
    )
}

// ---------------------------------------------------------------------------
// Invite dialogs
// ---------------------------------------------------------------------------

@Composable
private fun JoinByInviteDialog(onDismiss: () -> Unit, onJoin: (String) -> Unit) {
    var code by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        backgroundColor = SidebarBg,
        title = {
            Text(
                stringResource(Res.string.join_by_invite),
                color = TextPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold,
            )
        },
        text = {
            OutlinedTextField(
                value = code,
                onValueChange = { code = it.trim() },
                singleLine = true,
                label = { Text(stringResource(Res.string.invite_code), color = TextMuted, fontSize = 12.sp) },
                colors = TextFieldDefaults.outlinedTextFieldColors(
                    textColor = TextPrimary,
                    unfocusedBorderColor = FieldBg,
                    focusedBorderColor = Blurple,
                    backgroundColor = BottomBg,
                    cursorColor = Blurple,
                ),
                modifier = Modifier.fillMaxWidth(),
            )
        },
        confirmButton = {
            TextButton(onClick = { if (code.isNotBlank()) onJoin(code) }, enabled = code.isNotBlank()) {
                Text(stringResource(Res.string.join), color = Blurple)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.cancel), color = TextMuted)
            }
        },
    )
}

@Composable
private fun InviteCodeDialog(code: String, serverName: String, onDismiss: () -> Unit) {
    val clipboard = androidx.compose.ui.platform.LocalClipboardManager.current
    var copied by remember(code) { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        backgroundColor = SidebarBg,
        title = {
            Text(
                if (serverName.isNotEmpty()) "${stringResource(Res.string.create_invite)} — $serverName"
                else stringResource(Res.string.create_invite),
                color = TextPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold,
            )
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    stringResource(Res.string.invite_share_hint),
                    color = TextMuted, fontSize = 12.sp,
                )
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(4.dp))
                        .background(BottomBg)
                        .padding(horizontal = 10.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        text = code,
                        color = TextPrimary,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.SemiBold,
                        letterSpacing = 1.sp,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(onClick = {
                        clipboard.setText(androidx.compose.ui.text.AnnotatedString(code))
                        copied = true
                    }) {
                        Text(
                            if (copied) stringResource(Res.string.copied)
                            else stringResource(Res.string.copy),
                            color = Blurple,
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.cancel), color = TextMuted)
            }
        },
    )
}
