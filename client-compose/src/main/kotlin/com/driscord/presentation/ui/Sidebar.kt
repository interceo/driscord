package com.driscord.presentation.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.AppState
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import com.driscord.ui.*

@Composable
fun Sidebar(
    state: AppUiState,
    onIntent: (AppIntent) -> Unit,
    onGetPeerVolume: (String) -> Float,
) {
    var serverUrl by remember(state.config.serverUrl) { mutableStateOf(state.config.serverUrl) }
    var expandedPeer by remember { mutableStateOf<String?>(null) }
    Column(
        modifier =
            Modifier
                .width(240.dp)
                .fillMaxHeight()
                .background(SidebarBg),
    ) {
        // ── Server header ────────────────────────────────────────────────
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .background(SidebarBg)
                    .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                "DRISCORD",
                color = TextPrimary,
                fontSize = 14.sp,
                fontWeight = FontWeight.SemiBold,
                letterSpacing = 0.5.sp,
                modifier = Modifier.weight(1f),
            )
        }
        Divider(color = Color(0xFF1E1F22), thickness = 1.dp)

        // ── Members list ────────────────────────────────────────────────
        Column(
            modifier =
                Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState())
                    .padding(top = 8.dp),
        ) {
            Text(
                "MEMBERS — ${peers.size + if (state == AppState.Connected) 1 else 0}",
                color = TextMuted,
                fontSize = 10.sp,
                letterSpacing = 0.5.sp,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )

            if (state == AppState.Connected) {
                MemberRow(
                    id = localId,
                    label = "${if (localId.length > 14) localId.take(14) + "…" else localId} (you)",
                    online = true,
                    expanded = expandedPeer == localId,
                    onClick = { expandedPeer = if (expandedPeer == localId) null else localId },
                    isYou = true,
                    muted = muted,
                    deafened = deafened,
                    onGetVolume = { volume },
                    onSetVolume = onSetVolume,
                    onToggleMute = onToggleMute,
                    onToggleDeafen = onToggleDeafen,
                )
                if (expandedPeer == localId) {
                    MemberExpanded(
                        label = "Mic Volume",
                        value = volume,
                        level = inputLevel,
                        levelLabel = "Mic",
                        active = muted,
                        onChange = onSetVolume,
                    )
                }
            }

            peers.forEach { peer ->
                val label = if (peer.id.length > 14) peer.id.take(14) + "…" else peer.id
                MemberRow(
                    id = peer.id,
                    label = label,
                    online = peer.connected,
                    expanded = expandedPeer == peer.id,
                    onClick = { expandedPeer = if (expandedPeer == peer.id) null else peer.id },
                    onGetVolume = { onGetPeerVolume(peer.id) },
                    onSetVolume = { v -> onSetPeerVolume(peer.id, v) },
                )
                if (expandedPeer == peer.id) {
                    var pVol by remember(peer.id) { mutableStateOf(onGetPeerVolume(peer.id)) }
                    MemberExpanded(
                        label = "Volume",
                        value = pVol,
                        level = null,
                        levelLabel = null,
                        active = false,
                        onChange = { v ->
                            pVol = v
                            onSetPeerVolume(peer.id, v)
                        },
                    )
                }
            }

            if (state != AppState.Connected && peers.isEmpty()) {
                Text("  —", color = TextMuted, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 16.dp))
            }
        }

        // ── Voice / connect section ──────────────────────────────────────
        when (state) {
            AppState.Disconnected -> {
                Column(modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp)) {
                    Text("Server", color = TextMuted, fontSize = 11.sp, letterSpacing = 0.3.sp)
                    Spacer(Modifier.height(4.dp))
                    OutlinedTextField(
                        value = serverUrl,
                        onValueChange = { serverUrl = it },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors =
                            TextFieldDefaults.outlinedTextFieldColors(
                                textColor = TextPrimary,
                                unfocusedBorderColor = Color(0xFF40444B),
                                focusedBorderColor = Blurple,
                                backgroundColor = Color(0xFF1E1F22),
                                cursorColor = Blurple,
                            ),
                        textStyle = LocalTextStyle.current.copy(fontSize = 12.sp),
                    )
                    Spacer(Modifier.height(8.dp))
                    Button(
                        onClick = { onConnect(serverUrl) },
                        modifier = Modifier.fillMaxWidth().height(32.dp),
                        colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                        shape = RoundedCornerShape(4.dp),
                        contentPadding = PaddingValues(0.dp),
                    ) {
                        Text("Connect", color = Color.White, fontSize = 13.sp)
                    }
                }
            }

            AppState.Connecting -> {
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .background(VoiceBg)
                            .padding(horizontal = 12.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(14.dp),
                        strokeWidth = 2.dp,
                        color = Color(0xFFFAA61A),
                    )
                    Spacer(Modifier.width(8.dp))
                    Text("Connecting…", color = Color(0xFFFAA61A), fontSize = 13.sp)
                }
            }

            AppState.Connected -> {
                Column(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .background(VoiceBg)
                            .padding(horizontal = 12.dp, vertical = 8.dp),
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        // Signal bars
                        SignalBars(modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(
                            "Voice Connected",
                            color = Green,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.weight(1f),
                        )
                        // Signal quality indicator
                        SignalBars(modifier = Modifier.size(12.dp))
                        Spacer(Modifier.width(8.dp))
                        // Disconnect
                        Box(
                            modifier =
                                Modifier
                                    .size(20.dp)
                                    .clip(RoundedCornerShape(4.dp))
                                    .background(Color(0xFF40444B))
                                    .clickable(onClick = onDisconnect),
                            contentAlignment = Alignment.Center,
                        ) {
                            Text("✕", color = Red, fontSize = 10.sp)
                        }
                    }
                    Spacer(Modifier.height(2.dp))
                    val shortUrl = config.serverUrl.removePrefix("ws://").removePrefix("wss://")
                    Text(
                        "${peers.size + 1} connected  ·  $shortUrl",
                        color = TextMuted,
                        fontSize = 11.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    // Active share indicator
                    if (sharing) {
                        Spacer(Modifier.height(4.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Box(
                                modifier =
                                    Modifier
                                        .clip(RoundedCornerShape(2.dp))
                                        .background(Red)
                                        .padding(horizontal = 3.dp, vertical = 1.dp),
                            ) {
                                Text("LIVE", color = Color.White, fontSize = 8.sp, fontWeight = FontWeight.Bold)
                            }
                            Spacer(Modifier.width(5.dp))
                            Text(
                                shareTargetName.ifEmpty { "Screen" },
                                color = Green,
                                fontSize = 11.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                    }
                }
            }
        }

        Divider(color = Color(0xFF1E1F22), thickness = 1.dp)

        // ── Bottom user bar ──────────────────────────────────────────────
        Divider(color = Color(0xFF1E1F22), thickness = 1.dp)
        UserBar(
            localId = localId.ifEmpty { "—" },
            muted = muted,
            deafened = deafened,
            sharing = sharing,
            connected = state == AppState.Connected,
            onToggleMute = onToggleMute,
            onToggleDeafen = onToggleDeafen,
            onToggleShare = { if (sharing) onStopShare() else onStartShare() },
            onSettings = { showSettings = true },
        )
    }

    if (showSettings) {
        SettingsDialog(
            config = config,
            onDismiss = { showSettings = false },
            onSave = {
                onSaveConfig(it)
                showSettings = false
            },
        )
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
    onGetVolume: (() -> Float)? = null,
    onSetVolume: ((Float) -> Unit)? = null,
    onToggleMute: (() -> Unit)? = null,
    onToggleDeafen: (() -> Unit)? = null,
) {
    val bg = if (expanded) Color(0xFF35373C) else Color.Transparent

    var showMenu by remember { mutableStateOf(false) }
    var cursorPx by remember { mutableStateOf(IntOffset.Zero) }
    var vol by remember { mutableStateOf(1f) }
    var peerMuted by remember { mutableStateOf(false) }
    var savedVol by remember { mutableStateOf(1f) }

    // Avoid stale closure in pointerInput(Unit)
    val currentOnGetVolume by rememberUpdatedState(onGetVolume)

    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .background(bg)
                .then(
                    if (onGetVolume != null) {
                        Modifier.pointerInput(Unit) {
                            awaitPointerEventScope {
                                while (true) {
                                    val event = awaitPointerEvent()
                                    if (event.type == PointerEventType.Press && event.buttons.isSecondaryPressed) {
                                        val pos = event.changes.first().position
                                        cursorPx = IntOffset(pos.x.toInt(), pos.y.toInt())
                                        vol = currentOnGetVolume?.invoke() ?: 1f
                                        showMenu = true
                                    }
                                }
                            }
                        }
                    } else {
                        Modifier
                    },
                ),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onClick)
                    .padding(horizontal = 8.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            val avatarLetter = id.firstOrNull()?.uppercaseChar() ?: 'D'
            val avatarColor =
                remember(id) {
                    val h = id.fold(0x811c9dc5.toInt()) { acc, c -> (acc xor c.code) * 0x01000193.toInt() }
                    Color(
                        red = (80 + (h and 0x7F)) / 255f,
                        green = (80 + ((h shr 8) and 0x7F)) / 255f,
                        blue = (80 + ((h shr 16) and 0x7F)) / 255f,
                    )
                }
            Box(
                modifier = Modifier.size(28.dp).clip(CircleShape).background(avatarColor),
                contentAlignment = Alignment.Center,
            ) {
                Text(avatarLetter.toString(), color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.Bold)
            }
            Spacer(Modifier.width(8.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    label,
                    color = if (online) TextPrimary else TextMuted,
                    fontSize = 13.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Box(
                modifier =
                    Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(if (online) Green else Color(0xFF737F8D)),
            )
        }

        if (onGetVolume != null && onSetVolume != null) {
            // Zero-size anchor at cursor position for the context menu
            Box(modifier = Modifier.align(Alignment.TopStart).offset { cursorPx }.size(0.dp)) {
                DropdownMenu(
                    expanded = showMenu,
                    onDismissRequest = { showMenu = false },
                    modifier = Modifier.background(Color(0xFF2B2D31)).width(200.dp),
                ) {
                    Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
                        Text(
                            if (id.length > 20) id.take(20) + "…" else id,
                            color = Blurple,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.SemiBold,
                        )
                    }
                    Divider(color = Color(0xFF1E1F22))
                    if (isYou) {
                        Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
                            Column {
                                Text("Громкость микрофона", color = TextMuted, fontSize = 10.sp)
                                Slider(
                                    value = vol,
                                    onValueChange = { v ->
                                        vol = v
                                        onSetVolume(v)
                                    },
                                    valueRange = 0f..2f,
                                    colors = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
                                    modifier = Modifier.requiredWidth(176.dp).height(28.dp),
                                )
                            }
                        }
                        if (onToggleMute != null) {
                            DropdownMenuItem(onClick = { onToggleMute() }) {
                                Text("Заглушить", color = TextPrimary, fontSize = 13.sp, modifier = Modifier.weight(1f))
                                Checkbox(
                                    checked = muted,
                                    onCheckedChange = null,
                                    colors = CheckboxDefaults.colors(checkedColor = Blurple, uncheckedColor = TextMuted),
                                )
                            }
                        }
                        if (onToggleDeafen != null) {
                            DropdownMenuItem(onClick = { onToggleDeafen() }) {
                                Text("Откл. звук", color = TextPrimary, fontSize = 13.sp, modifier = Modifier.weight(1f))
                                Checkbox(
                                    checked = deafened,
                                    onCheckedChange = null,
                                    colors = CheckboxDefaults.colors(checkedColor = Blurple, uncheckedColor = TextMuted),
                                )
                            }
                        }
                    } else {
                        Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
                            Column {
                                Text("Громкость", color = TextMuted, fontSize = 10.sp)
                                Slider(
                                    value = vol,
                                    onValueChange = { v ->
                                        vol = v
                                        peerMuted = false
                                        onSetVolume(v)
                                    },
                                    valueRange = 0f..2f,
                                    colors = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
                                    modifier = Modifier.requiredWidth(176.dp).height(28.dp),
                                )
                            }
                        }
                        DropdownMenuItem(onClick = {
                            if (peerMuted) {
                                vol = savedVol
                                onSetVolume(savedVol)
                            } else {
                                savedVol = vol.takeIf { it > 0f } ?: 1f
                                vol = 0f
                                onSetVolume(0f)
                            }
                            peerMuted = !peerMuted
                        }) {
                            Text("Заглушить", color = TextPrimary, fontSize = 13.sp, modifier = Modifier.weight(1f))
                            Checkbox(
                                checked = peerMuted,
                                onCheckedChange = null,
                                colors = CheckboxDefaults.colors(checkedColor = Blurple, uncheckedColor = TextMuted),
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MemberExpanded(
    label: String,
    value: Float,
    level: Float?,
    levelLabel: String?,
    active: Boolean,
    onChange: (Float) -> Unit,
) {
    Column(modifier = Modifier.padding(start = 44.dp, end = 8.dp, bottom = 4.dp)) {
        Text(label, color = TextMuted, fontSize = 10.sp)
        Slider(
            value = value,
            onValueChange = onChange,
            valueRange = 0f..2f,
            colors = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
            modifier = Modifier.height(28.dp),
        )
        if (level != null && levelLabel != null) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(levelLabel, color = TextMuted, fontSize = 10.sp, modifier = Modifier.width(20.dp))
                LinearProgressIndicator(
                    progress = level.coerceIn(0f, 1f),
                    modifier = Modifier.width(80.dp).height(6.dp),
                    color = if (active) Red else Green,
                    backgroundColor = Color(0xFF40444B),
                )
            }
        }
    }
}

// ---------------------------------------------------------------------------
// User bar (bottom)
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
    val short = if (localId.length > 12) localId.take(12) + "…" else localId
    val avatarLetter = localId.firstOrNull()?.uppercaseChar() ?: 'D'

    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .background(BottomBg)
                .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val avatarColor =
            remember(localId) {
                val h = localId.fold(0x811c9dc5.toInt()) { acc, c -> (acc xor c.code) * 0x01000193.toInt() }
                Color(
                    red = (80 + (h and 0x7F)) / 255f,
                    green = (80 + ((h shr 8) and 0x7F)) / 255f,
                    blue = (80 + ((h shr 16) and 0x7F)) / 255f,
                )
            }
        Box(
            modifier = Modifier.size(28.dp).clip(CircleShape).background(avatarColor),
            contentAlignment = Alignment.Center,
        ) {
            Text(avatarLetter.toString(), color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.width(8.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                short,
                color = TextPrimary,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(if (connected) "Connected" else "Offline", color = TextMuted, fontSize = 10.sp)
        }
        if (connected) {
            BottomIconBtn(label = if (muted) "M!" else "M", active = muted, activeColor = Red, onClick = onToggleMute)
            Spacer(Modifier.width(2.dp))
            BottomIconBtn(label = if (deafened) "H!" else "H", active = deafened, activeColor = Red, onClick = onToggleDeafen)
            Spacer(Modifier.width(2.dp))
            BottomIconBtn(label = if (sharing) "◼" else "◻", active = sharing, activeColor = Green, onClick = onToggleShare)
            Spacer(Modifier.width(2.dp))
        }
        BottomIconBtn(label = "⚙", active = false, activeColor = Red, onClick = onSettings)
    }
}

@Composable
private fun BottomIconBtn(
    label: String,
    active: Boolean,
    activeColor: Color,
    onClick: () -> Unit,
) {
    Box(
        modifier =
            Modifier
                .size(26.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(if (active) activeColor.copy(alpha = 0.2f) else Color.Transparent)
                .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (active) activeColor else TextMuted, fontSize = 11.sp, fontWeight = FontWeight.Bold)
    }
}

// ---------------------------------------------------------------------------
// Signal bars decoration
// ---------------------------------------------------------------------------

@Composable
private fun SignalBars(modifier: Modifier = Modifier) {
    Row(modifier = modifier, verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(1.dp)) {
        repeat(3) { i ->
            Box(
                modifier =
                    Modifier
                        .width(3.dp)
                        .height((4 + i * 3).dp)
                        .clip(RoundedCornerShape(1.dp))
                        .background(Green),
            )
        }
    }
}
