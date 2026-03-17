package com.driscord.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.driscord.AppState
import com.driscord.CaptureTarget
import com.driscord.PeerInfo
import com.driscord.StreamStats

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
private val ContentBg   = Color(0xFF313338)
private val TileBg      = Color(0xFF2B2D31)
private val TileHover   = Color(0xFF383A40)
private val Green       = Color(0xFF3BA55C)
private val Red         = Color(0xFFED4245)
private val Blurple     = Color(0xFF5865F2)
private val TextW       = Color(0xFFDCDDDE)
private val TextMuted   = Color(0xFF72767D)

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
    externalShareDialog: Boolean = false,
    onShareDialogDismiss: () -> Unit = {},
) {
    if (state != AppState.Connected) {
        // Not connected — show placeholder
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

    var showShareDialog  by remember { mutableStateOf(false) }
    var focusedPeer      by remember { mutableStateOf<String?>(null) }

    // Sync with external trigger (from sidebar Share button)
    LaunchedEffect(externalShareDialog) { if (externalShareDialog) showShareDialog = true }

    Column(
        modifier = Modifier.fillMaxSize().background(ContentBg),
    ) {

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

        // ── Content grid ──────────────────────────────────────────────────
        val allPeers = buildList {
            if (localId.isNotEmpty()) add(localId)
            peers.forEach { add(it.id) }
        }
        val totalItems = allPeers.size + streamingPeers.size

        if (focusedPeer != null && streamingPeers.contains(focusedPeer)) {
            // Fullscreen stream view
            Box(modifier = Modifier.fillMaxSize().padding(12.dp)) {
                StreamTile(
                    peerId          = focusedPeer!!,
                    bitmap          = frames[focusedPeer!!],
                    watching        = watching,
                    stats           = streamStats,
                    streamVolume    = onStreamVolume(),
                    onSetStreamVolume = onSetStreamVolume,
                    onClick         = { focusedPeer = null },
                    onJoin          = onJoinStream,
                    onLeave         = onLeaveStream,
                    modifier        = Modifier.fillMaxSize(),
                )
            }
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
                items(allPeers) { peerId ->
                    val isYou = peerId == localId
                    val peer  = peers.find { it.id == peerId }
                    UserTile(
                        peerId      = peerId,
                        label       = buildString {
                            val s = if (peerId.length > 12) peerId.take(12) + "…" else peerId
                            append(s)
                            if (isYou) append(" (you)")
                        },
                        online      = isYou || (peer?.connected == true),
                        isStreaming = streamingPeers.contains(peerId),
                    )
                }
                items(streamingPeers) { peerId ->
                    StreamTile(
                        peerId          = peerId,
                        bitmap          = frames[peerId],
                        watching        = watching,
                        stats           = streamStats,
                        streamVolume    = onStreamVolume(),
                        onSetStreamVolume = onSetStreamVolume,
                        onClick         = { focusedPeer = peerId },
                        onJoin          = onJoinStream,
                        onLeave         = onLeaveStream,
                    )
                }
            }
        }
    }

    if (showShareDialog) {
        ShareDialog(
            systemAudioAvailable = systemAudioAvailable,
            onListTargets    = onListTargets,
            onGrabThumbnail  = onGrabThumbnail,
            onDismiss        = { showShareDialog = false; onShareDialogDismiss() },
            onGo = { target, quality, fps, audio ->
                onStartSharing(target, quality, fps, audio)
                showShareDialog = false
                onShareDialogDismiss()
            },
        )
    }
}

// ---------------------------------------------------------------------------
// User tile
// ---------------------------------------------------------------------------

@Composable
private fun UserTile(peerId: String, label: String, online: Boolean, isStreaming: Boolean) {
    val avatarColor = remember(peerId) {
        val h = peerId.fold(0x811c9dc5.toInt()) { acc, c -> (acc xor c.code) * 0x01000193.toInt() }
        Color(
            red   = (60 + (h and 0x7F)) / 255f,
            green = (60 + ((h shr 8) and 0x7F)) / 255f,
            blue  = (60 + ((h shr 16) and 0x7F)) / 255f,
        )
    }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .aspectRatio(16f / 9f)
            .clip(RoundedCornerShape(8.dp))
            .background(TileBg),
        contentAlignment = Alignment.Center,
    ) {
        // Subtle gradient background per user
        Box(modifier = Modifier.fillMaxSize().background(avatarColor.copy(alpha = 0.35f)))

        // Avatar circle
        val avatarLetter = peerId.firstOrNull()?.uppercaseChar() ?: '?'
        Box(
            modifier = Modifier.size(48.dp).clip(CircleShape).background(avatarColor),
            contentAlignment = Alignment.Center,
        ) {
            Text(avatarLetter.toString(), color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        }

        // Name label at bottom
        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .fillMaxWidth()
                .background(Color(0x99000000))
                .padding(horizontal = 8.dp, vertical = 4.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(7.dp)
                        .clip(CircleShape)
                        .background(if (online) Green else TextMuted),
                )
                Spacer(Modifier.width(5.dp))
                Text(
                    label,
                    color = Color.White,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }

        if (isStreaming) {
            LiveBadge(modifier = Modifier.align(Alignment.TopEnd).padding(6.dp))
        }
    }
}

// ---------------------------------------------------------------------------
// Stream tile
// ---------------------------------------------------------------------------

@Composable
private fun StreamTile(
    peerId: String,
    bitmap: ImageBitmap?,
    watching: Boolean,
    stats: StreamStats,
    streamVolume: Float,
    onSetStreamVolume: (Float) -> Unit,
    onClick: () -> Unit,
    onJoin: () -> Unit,
    onLeave: () -> Unit,
    modifier: Modifier = Modifier
        .fillMaxWidth()
        .aspectRatio(16f / 9f),
) {
    var showPopup by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF111214))
            .combinedClickable(
                onClick    = { if (watching) onClick() else showPopup = true },
                onLongClick = { showPopup = true },
            ),
        contentAlignment = Alignment.Center,
    ) {
        if (bitmap != null) {
            Image(
                bitmap = bitmap,
                contentDescription = "Stream from $peerId",
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Fit,
            )
            if (stats.width > 0) StatsOverlay(stats)
        } else {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
                modifier = Modifier.fillMaxSize(),
            ) {
                Text(if (watching) "Buffering…" else "📺", color = TextMuted,
                    fontSize = if (watching) 12.sp else 28.sp)
                if (!watching) {
                    Spacer(Modifier.height(4.dp))
                    Text("Click to watch", color = TextMuted, fontSize = 11.sp)
                }
            }
        }

        LiveBadge(modifier = Modifier.align(Alignment.TopEnd).padding(6.dp))

        if (watching) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(6.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(Color(0x99000000))
                    .clickable { onLeave() }
                    .padding(horizontal = 6.dp, vertical = 2.dp),
            ) {
                Text("LEAVE", color = Red, fontSize = 9.sp, fontWeight = FontWeight.Bold)
            }
        }

        // Name at bottom
        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .fillMaxWidth()
                .background(Color(0x99000000))
                .padding(horizontal = 8.dp, vertical = 3.dp),
        ) {
            val short = if (peerId.length > 16) peerId.take(16) + "…" else peerId
            Text(short, color = Color.White, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }

    if (showPopup) {
        var vol by remember { mutableStateOf(streamVolume) }
        AlertDialog(
            onDismissRequest = { showPopup = false },
            backgroundColor = Color(0xFF2B2D31),
            title = {
                val short = if (peerId.length > 18) peerId.take(18) + "…" else peerId
                Text(short, color = Blurple, fontSize = 14.sp)
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (!watching) {
                        Text("Watch this stream?", color = TextW, fontSize = 13.sp)
                        Button(
                            onClick = { onJoin(); showPopup = false },
                            colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(4.dp),
                        ) { Text("▶  Watch", color = Color.White) }
                    } else {
                        Text("Stream Volume", color = TextW, fontSize = 12.sp)
                        Slider(
                            value = vol,
                            onValueChange = { vol = it; onSetStreamVolume(it) },
                            valueRange = 0f..2f,
                            colors = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
                        )
                        Button(
                            onClick = { onLeave(); showPopup = false },
                            colors = ButtonDefaults.buttonColors(backgroundColor = Red),
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(4.dp),
                        ) { Text("Leave Stream", color = Color.White) }
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { showPopup = false }) { Text("Cancel", color = TextMuted) }
            },
        )
    }
}

// ---------------------------------------------------------------------------
// LIVE badge
// ---------------------------------------------------------------------------

@Composable
fun LiveBadge(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(3.dp))
            .background(Red)
            .padding(horizontal = 4.dp, vertical = 1.dp),
    ) {
        Text("LIVE", color = Color.White, fontSize = 9.sp, fontWeight = FontWeight.Bold)
    }
}

// ---------------------------------------------------------------------------
// Stats overlay
// ---------------------------------------------------------------------------

@Composable
private fun StatsOverlay(stats: StreamStats) {
    Box(
        modifier = Modifier
            .padding(6.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(Color.Black.copy(alpha = 0.75f))
            .padding(horizontal = 6.dp, vertical = 3.dp),
    ) {
        Column {
            Text(
                "${stats.width}×${stats.height}  H.264  ${stats.measuredKbps} kbps",
                color = Color(0xFFDCDCDC), fontSize = 10.sp,
            )
            val warn = stats.video.misses > 0 || stats.audio.misses > 0
            Text(
                "V: q=${stats.video.queue} ${stats.video.bufMs}ms  A: q=${stats.audio.queue} ${stats.audio.bufMs}ms",
                color = if (warn) Color(0xFFFFC800) else Color(0xFF3BA55C),
                fontSize = 10.sp,
            )
        }
    }
}

// ---------------------------------------------------------------------------
// Share dialog
// ---------------------------------------------------------------------------

@Composable
fun ShareDialog(
    systemAudioAvailable: Boolean,
    onListTargets: () -> List<CaptureTarget>,
    onGrabThumbnail: (CaptureTarget) -> ImageBitmap?,
    onDismiss: () -> Unit,
    onGo: (CaptureTarget, Int, Int, Boolean) -> Unit,
) {
    val targets    = remember { onListTargets() }
    val thumbnails = remember {
        mutableStateMapOf<Int, ImageBitmap?>().also { map ->
            targets.forEachIndexed { i, t -> map[i] = onGrabThumbnail(t) }
        }
    }

    var selectedIdx by remember { mutableStateOf(-1) }
    var quality     by remember { mutableStateOf(2) }
    var fps         by remember { mutableStateOf(30) }
    var shareAudio  by remember { mutableStateOf(true) }

    val qualities  = listOf("Source", "720p", "1080p", "1440p")
    val fpsOptions = listOf(15, 30, 60)

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.size(700.dp, 520.dp),
            shape = RoundedCornerShape(8.dp),
            color = Color(0xFF2B2D31),
            elevation = 8.dp,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Choose what to share", color = TextW, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(8.dp))
                Divider(color = Color(0xFF1E1F22))
                Spacer(Modifier.height(8.dp))

                LazyVerticalGrid(
                    columns = GridCells.Adaptive(160.dp),
                    modifier = Modifier.weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(targets.indices.toList()) { idx ->
                        val selected = idx == selectedIdx
                        Column(
                            modifier = Modifier
                                .clip(RoundedCornerShape(4.dp))
                                .border(2.dp, if (selected) Blurple else Color.Transparent, RoundedCornerShape(4.dp))
                                .clickable { selectedIdx = idx }
                                .padding(4.dp),
                        ) {
                            val thumb = thumbnails[idx]
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .aspectRatio(16f / 9f)
                                    .clip(RoundedCornerShape(3.dp))
                                    .background(Color(0xFF111214)),
                                contentAlignment = Alignment.Center,
                            ) {
                                if (thumb != null) {
                                    Image(bitmap = thumb, contentDescription = null,
                                        modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
                                } else {
                                    Text("📺", fontSize = 20.sp)
                                }
                            }
                            Text(
                                targets[idx].name.take(24),
                                color = TextW, fontSize = 11.sp, maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(top = 2.dp),
                            )
                        }
                    }
                }

                Spacer(Modifier.height(8.dp))
                Divider(color = Color(0xFF1E1F22))
                Spacer(Modifier.height(8.dp))

                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Column {
                        Text("Quality", color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(qualities, quality) { quality = it }
                    }
                    Column {
                        Text("FPS", color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(fpsOptions.map { "$it" }, fpsOptions.indexOf(fps)) { fps = fpsOptions[it] }
                    }
                    if (systemAudioAvailable) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = shareAudio, onCheckedChange = { shareAudio = it },
                                colors = CheckboxDefaults.colors(checkedColor = Blurple),
                            )
                            Text("Share Audio", color = TextW, fontSize = 13.sp)
                        }
                    }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onDismiss) { Text("Cancel", color = TextMuted) }
                    Button(
                        onClick = { if (selectedIdx >= 0) onGo(targets[selectedIdx], quality, fps, shareAudio) },
                        enabled = selectedIdx >= 0,
                        colors = ButtonDefaults.buttonColors(backgroundColor = Green),
                        shape = RoundedCornerShape(4.dp),
                    ) {
                        Text("Go Live", color = Color.White)
                    }
                }
            }
        }
    }
}

@Composable
fun DropdownSelect(options: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(
            onClick = { expanded = true },
            shape = RoundedCornerShape(4.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = TextW),
            modifier = Modifier.height(28.dp),
            contentPadding = PaddingValues(horizontal = 8.dp),
        ) {
            Text(options.getOrElse(selectedIndex) { "" }, fontSize = 12.sp)
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.background(Color(0xFF2B2D31)),
        ) {
            options.forEachIndexed { i, label ->
                DropdownMenuItem(onClick = { onSelect(i); expanded = false }) {
                    Text(label, color = TextW, fontSize = 12.sp)
                }
            }
        }
    }
}
