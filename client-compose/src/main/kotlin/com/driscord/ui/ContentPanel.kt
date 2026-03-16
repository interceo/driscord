package com.driscord.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.driscord.AppState
import com.driscord.CaptureTarget
import com.driscord.PeerInfo
import com.driscord.StreamStats

private val Green = Color(0xFF3BA55C)
private val Red = Color(0xFFED4245)
private val Blurple = Color(0xFF5865F2)
private val TextMuted = Color(0xFF72767D)

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
) {
    if (state != AppState.Connected) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("Connect to a server to begin", color = TextMuted, fontSize = 14.sp)
        }
        return
    }

    var showShareDialog by remember { mutableStateOf(false) }
    var focusedStreamPeer by remember { mutableStateOf<String?>(null) }

    Column(modifier = Modifier.fillMaxSize()) {
        // Top toolbar
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (!sharing) {
                Button(
                    onClick = { showShareDialog = true },
                    colors = ButtonDefaults.buttonColors(backgroundColor = Green),
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 12.dp),
                ) {
                    Text("Share Screen", color = Color.White, fontSize = 13.sp)
                }
            } else {
                Button(
                    onClick = onStopSharing,
                    colors = ButtonDefaults.buttonColors(backgroundColor = Red),
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 12.dp),
                ) {
                    Text("Stop Sharing", color = Color.White, fontSize = 13.sp)
                }
                Spacer(Modifier.width(8.dp))
                Text("LIVE", color = Green, fontSize = 13.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.Bold)
            }
        }

        Spacer(Modifier.height(8.dp))
        Divider(color = Color(0xFF40444B))
        Spacer(Modifier.height(8.dp))

        // Video grid
        val allPeers = buildList {
            if (localId.isNotEmpty()) add(localId)
            peers.forEach { add(it.id) }
        }

        if (focusedStreamPeer != null && streamingPeers.contains(focusedStreamPeer)) {
            // Full-screen stream view
            StreamTile(
                peerId = focusedStreamPeer!!,
                bitmap = frames[focusedStreamPeer!!],
                watching = watching,
                stats = streamStats,
                streamVolume = onStreamVolume(),
                onSetStreamVolume = onSetStreamVolume,
                onClick = { focusedStreamPeer = null },
                onJoin = onJoinStream,
                onLeave = onLeaveStream,
                modifier = Modifier.fillMaxSize(),
            )
        } else {
            val cols = when {
                allPeers.size + streamingPeers.size >= 9 -> 4
                allPeers.size + streamingPeers.size >= 4 -> 3
                allPeers.size + streamingPeers.size >= 2 -> 2
                else -> 1
            }

            LazyVerticalGrid(
                columns = GridCells.Fixed(cols),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(allPeers) { peerId ->
                    val isYou = peerId == localId
                    UserTile(
                        peerId = peerId,
                        label = buildString {
                            val s = if (peerId.length > 10) peerId.take(10) + "…" else peerId
                            append(s)
                            if (isYou) append(" (you)")
                        },
                        isStreaming = streamingPeers.contains(peerId),
                    )
                }
                items(streamingPeers) { peerId ->
                    StreamTile(
                        peerId = peerId,
                        bitmap = frames[peerId],
                        watching = watching,
                        stats = streamStats,
                        streamVolume = onStreamVolume(),
                        onSetStreamVolume = onSetStreamVolume,
                        onClick = { focusedStreamPeer = peerId },
                        onJoin = onJoinStream,
                        onLeave = onLeaveStream,
                    )
                }
            }
        }
    }

    if (showShareDialog) {
        ShareDialog(
            systemAudioAvailable = systemAudioAvailable,
            onListTargets = onListTargets,
            onGrabThumbnail = onGrabThumbnail,
            onDismiss = { showShareDialog = false },
            onGo = { target, quality, fps, audio ->
                onStartSharing(target, quality, fps, audio)
                showShareDialog = false
            },
        )
    }
}

// ---------------------------------------------------------------------------
// User tile (avatar/placeholder)
// ---------------------------------------------------------------------------

@Composable
private fun UserTile(peerId: String, label: String, isStreaming: Boolean) {
    val color = remember(peerId) {
        val h = peerId.fold(0x811c9dc5.toInt()) { acc, c -> (acc xor c.code) * 0x01000193.toInt() }
        Color(
            red = (80 + (h and 0x7F)) / 255f,
            green = (80 + ((h shr 8) and 0x7F)) / 255f,
            blue = (80 + ((h shr 16) and 0x7F)) / 255f,
        )
    }
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .aspectRatio(16f / 9f)
            .clip(RoundedCornerShape(6.dp))
            .background(color),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = Color.White.copy(alpha = 0.9f), fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
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
    var showVolPopup by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(6.dp))
            .background(Color(0xFF18191C))
            .combinedClickable(
                onClick = onClick,
                onLongClick = { showVolPopup = true },
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
            // Stats overlay
            if (stats.width > 0) {
                StatsOverlay(stats)
            }
        } else {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    if (watching) "Buffering…" else "Right-click to watch",
                    color = TextMuted,
                    fontSize = 12.sp,
                )
            }
        }
        LiveBadge(modifier = Modifier.align(Alignment.TopEnd).padding(6.dp))
        if (!watching) {
            Text(
                "MUTE",
                color = Color(0xFFB0B0B0),
                fontSize = 10.sp,
                modifier = Modifier.align(Alignment.TopStart).padding(6.dp),
            )
        }
    }

    if (showVolPopup) {
        var vol by remember { mutableStateOf(streamVolume) }
        AlertDialog(
            onDismissRequest = { showVolPopup = false },
            title = { Text(peerId.take(14), color = Blurple, fontSize = 14.sp) },
            text = {
                Column {
                    if (watching) {
                        Text("Stream Volume", color = Color.White, fontSize = 13.sp)
                        Slider(
                            value = vol,
                            onValueChange = { vol = it; onSetStreamVolume(it) },
                            valueRange = 0f..2f,
                        )
                    }
                    Spacer(Modifier.height(8.dp))
                    if (watching) {
                        Button(
                            onClick = { onLeave(); showVolPopup = false },
                            colors = ButtonDefaults.buttonColors(backgroundColor = Red),
                            modifier = Modifier.fillMaxWidth(),
                        ) { Text("Leave Stream", color = Color.White) }
                    } else {
                        Button(
                            onClick = { onJoin(); showVolPopup = false },
                            colors = ButtonDefaults.buttonColors(backgroundColor = Green),
                            modifier = Modifier.fillMaxWidth(),
                        ) { Text("Watch Stream", color = Color.White) }
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { showVolPopup = false }) { Text("Close") }
            },
            backgroundColor = Color(0xFF2C2F33),
        )
    }
}

@Composable
private fun LiveBadge(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(3.dp))
            .background(Red)
            .padding(horizontal = 4.dp, vertical = 1.dp),
    ) {
        Text("LIVE", color = Color.White, fontSize = 10.sp)
    }
}

@Composable
private fun StatsOverlay(stats: StreamStats) {
    Box(
        modifier = Modifier
            .padding(6.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(Color.Black.copy(alpha = 0.7f))
            .padding(horizontal = 6.dp, vertical = 3.dp),
    ) {
        Column {
            Text(
                "${stats.width}×${stats.height}  H.264  ${stats.measuredKbps} kbps",
                color = Color(0xFFDCDCDC),
                fontSize = 10.sp,
            )
            val warn = stats.video.misses > 0 || stats.audio.misses > 0
            Text(
                "V: q=${stats.video.queue} buf=${stats.video.bufMs}ms  " +
                        "A: q=${stats.audio.queue} buf=${stats.audio.bufMs}ms",
                color = if (warn) Color(0xFFFFC800) else Color(0xFFA0C8A0),
                fontSize = 10.sp,
            )
        }
    }
}

// ---------------------------------------------------------------------------
// Share dialog
// ---------------------------------------------------------------------------

@Composable
private fun ShareDialog(
    systemAudioAvailable: Boolean,
    onListTargets: () -> List<CaptureTarget>,
    onGrabThumbnail: (CaptureTarget) -> ImageBitmap?,
    onDismiss: () -> Unit,
    onGo: (CaptureTarget, Int, Int, Boolean) -> Unit,
) {
    val targets = remember { onListTargets() }
    val thumbnails = remember {
        mutableStateMapOf<Int, ImageBitmap?>().also { map ->
            targets.forEachIndexed { i, t -> map[i] = onGrabThumbnail(t) }
        }
    }

    var selectedIdx by remember { mutableStateOf(-1) }
    var quality by remember { mutableStateOf(2) }  // 2 = FHD_1080
    var fps by remember { mutableStateOf(30) }
    var shareAudio by remember { mutableStateOf(true) }

    val qualities = listOf("Source", "720p", "1080p", "1440p")
    val fpsOptions = listOf(15, 30, 60)

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.size(700.dp, 500.dp),
            shape = RoundedCornerShape(8.dp),
            color = Color(0xFF2C2F33),
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Choose what to share", color = Color.White, fontSize = 16.sp)
                Spacer(Modifier.height(8.dp))
                Divider(color = Color(0xFF40444B))
                Spacer(Modifier.height(8.dp))

                // Thumbnail grid
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
                                .border(
                                    2.dp,
                                    if (selected) Blurple else Color.Transparent,
                                    RoundedCornerShape(4.dp),
                                )
                                .clickable { selectedIdx = idx }
                                .padding(4.dp),
                        ) {
                            val thumb = thumbnails[idx]
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .aspectRatio(16f / 9f)
                                    .clip(RoundedCornerShape(3.dp))
                                    .background(Color(0xFF18191C)),
                                contentAlignment = Alignment.Center,
                            ) {
                                if (thumb != null) {
                                    Image(
                                        bitmap = thumb,
                                        contentDescription = null,
                                        modifier = Modifier.fillMaxSize(),
                                        contentScale = ContentScale.Fit,
                                    )
                                }
                            }
                            Text(
                                targets[idx].name.take(22),
                                color = Color(0xFFDCDDDE),
                                fontSize = 11.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(top = 2.dp),
                            )
                        }
                    }
                }

                Spacer(Modifier.height(8.dp))
                Divider(color = Color(0xFF40444B))
                Spacer(Modifier.height(8.dp))

                // Bottom bar
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    // Quality
                    Column {
                        Text("Quality", color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(qualities, quality) { quality = it }
                    }
                    // FPS
                    Column {
                        Text("FPS", color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(fpsOptions.map { "$it" }, fpsOptions.indexOf(fps)) { fps = fpsOptions[it] }
                    }
                    if (systemAudioAvailable) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = shareAudio,
                                onCheckedChange = { shareAudio = it },
                                colors = CheckboxDefaults.colors(checkedColor = Blurple),
                            )
                            Text("Share Audio", color = Color.White, fontSize = 13.sp)
                        }
                    }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onDismiss) { Text("Cancel", color = TextMuted) }
                    Button(
                        onClick = {
                            if (selectedIdx >= 0) onGo(targets[selectedIdx], quality, fps, shareAudio)
                        },
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
private fun DropdownSelect(options: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(
            onClick = { expanded = true },
            shape = RoundedCornerShape(4.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White),
            modifier = Modifier.height(28.dp),
            contentPadding = PaddingValues(horizontal = 8.dp),
        ) {
            Text(options.getOrElse(selectedIndex) { "" }, fontSize = 12.sp)
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.background(Color(0xFF2C2F33)),
        ) {
            options.forEachIndexed { i, label ->
                DropdownMenuItem(onClick = { onSelect(i); expanded = false }) {
                    Text(label, color = Color.White, fontSize = 12.sp)
                }
            }
        }
    }
}
