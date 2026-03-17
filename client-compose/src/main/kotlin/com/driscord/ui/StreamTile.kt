package com.driscord.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.StreamStats

@Composable
internal fun StreamTile(
    peerId: String,
    bitmap: ImageBitmap?,
    watching: Boolean,
    stats: StreamStats,
    streamVolume: Float,
    onSetStreamVolume: (Float) -> Unit,
    onClick: () -> Unit,
    onJoin: () -> Unit,
    onLeave: () -> Unit,
    modifier: Modifier =
        Modifier
            .fillMaxWidth()
            .aspectRatio(16f / 9f),
) {
    var showMenu by remember { mutableStateOf(false) }
    var cursorPx by remember { mutableStateOf(IntOffset.Zero) }
    var vol by remember { mutableStateOf(streamVolume) }
    var isMuted by remember { mutableStateOf(false) }
    var savedVol by remember { mutableStateOf(1f) }

    // Avoid stale closure in pointerInput(Unit)
    val currentStreamVolume by rememberUpdatedState(streamVolume)

    Box(
        modifier =
            modifier
                .clip(RoundedCornerShape(8.dp))
                .background(Color(0xFF111214))
                .combinedClickable(onClick = { if (watching) onClick() else onJoin() })
                .pointerInput(Unit) {
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            if (event.type == PointerEventType.Press && event.buttons.isSecondaryPressed) {
                                val pos = event.changes.first().position
                                cursorPx = IntOffset(pos.x.toInt(), pos.y.toInt())
                                vol = currentStreamVolume
                                showMenu = true
                            }
                        }
                    }
                },
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
                Text(
                    if (watching) "Buffering…" else "📺",
                    color = TextMuted,
                    fontSize = if (watching) 12.sp else 28.sp,
                )
                if (!watching) {
                    Spacer(Modifier.height(4.dp))
                    Text("Click to watch", color = TextMuted, fontSize = 11.sp)
                }
            }
        }

        LiveBadge(modifier = Modifier.align(Alignment.TopEnd).padding(6.dp))

        Box(
            modifier =
                Modifier
                    .align(Alignment.BottomStart)
                    .fillMaxWidth()
                    .background(Color(0x99000000))
                    .padding(horizontal = 8.dp, vertical = 3.dp),
        ) {
            val short = if (peerId.length > 16) peerId.take(16) + "…" else peerId
            Text(short, color = Color.White, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }

        Box(modifier = Modifier.align(Alignment.TopStart).offset { cursorPx }.size(0.dp)) {
            DropdownMenu(
                expanded = showMenu,
                onDismissRequest = { showMenu = false },
                modifier = Modifier.background(Color(0xFF2B2D31)).width(200.dp),
            ) {
                Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
                    Text(
                        if (peerId.length > 20) peerId.take(20) + "…" else peerId,
                        color = Blurple,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                }
                Divider(color = Color(0xFF1E1F22))
                if (!watching) {
                    DropdownMenuItem(onClick = {
                        onJoin()
                        showMenu = false
                    }) {
                        Text("▶  Смотреть", color = Green, fontSize = 13.sp)
                    }
                } else {
                    VolumeSliderItem(label = "Громкость", vol = vol, onVolume = { v ->
                        vol = v
                        isMuted = false
                        onSetStreamVolume(v)
                    })
                    CheckboxItem("Заглушить", isMuted) {
                        if (isMuted) {
                            vol = savedVol
                            onSetStreamVolume(savedVol)
                        } else {
                            savedVol = vol.takeIf { it > 0f } ?: 1f
                            vol = 0f
                            onSetStreamVolume(0f)
                        }
                        isMuted = !isMuted
                    }
                    Divider(color = Color(0xFF1E1F22))
                    DropdownMenuItem(onClick = {
                        onLeave()
                        showMenu = false
                    }) {
                        Text("Покинуть трансляцию", color = Red, fontSize = 13.sp)
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LIVE badge
// ---------------------------------------------------------------------------

@Composable
internal fun LiveBadge(modifier: Modifier = Modifier) {
    Box(
        modifier =
            modifier
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
internal fun StatsOverlay(stats: StreamStats) {
    Box(
        modifier =
            Modifier
                .padding(6.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(Color.Black.copy(alpha = 0.75f))
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
                "V: q=${stats.video.queue} ${stats.video.bufMs}ms  A: q=${stats.audio.queue} ${stats.audio.bufMs}ms",
                color = if (warn) Color(0xFFFFC800) else Color(0xFF3BA55C),
                fontSize = 10.sp,
            )
        }
    }
}
