package com.driscord.presentation.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.Divider
import androidx.compose.material.DropdownMenuItem
import androidx.compose.material.Text
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
import com.driscord.domain.model.StreamStats
import com.driscord.presentation.ui.components.*
import com.driscord.ui.*

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
    modifier: Modifier = Modifier,
) {
    var vol by remember { mutableStateOf(streamVolume) }
    var isMuted by remember { mutableStateOf(false) }
    var savedVol by remember { mutableStateOf(1f) }
    val currentStreamVolume by rememberUpdatedState(streamVolume)

    RightClickMenuHost(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF111214))
            .combinedClickable(onClick = { if (watching) onClick() else onJoin() }),
        onMenuOpened = {
            // Refresh volume each time the menu opens so the slider isn't stale
            val current = currentStreamVolume
            vol = current
            isMuted = current == 0f
        },
        menuContent = { dismiss ->
            MenuHeader(peerId)
            if (!watching) {
                DropdownMenuItem(onClick = { onJoin(); dismiss() }) {
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
                Divider(color = DividerColor)
                DropdownMenuItem(onClick = { onLeave(); dismiss() }) {
                    Text("Покинуть трансляцию", color = Red, fontSize = 13.sp)
                }
            }
        },
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
                    text = if (watching) "Buffering…" else "📺",
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
            modifier = Modifier
                .align(Alignment.BottomStart)
                .fillMaxWidth()
                .background(TileOverlay)
                .padding(horizontal = 8.dp, vertical = 3.dp),
        ) {
            val short = if (peerId.length > 16) peerId.take(16) + "…" else peerId
            Text(short, color = Color.White, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

// ---------------------------------------------------------------------------
// Stats overlay — private, only used by StreamTile
// ---------------------------------------------------------------------------

@Composable
private fun StatsOverlay(stats: StreamStats) {
    Box(
        modifier = Modifier
            .padding(6.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(StatsOverlayBg.copy(alpha = 0.75f))
            .padding(horizontal = 6.dp, vertical = 3.dp),
    ) {
        Column {
            Text(
                text = "${stats.width}×${stats.height}  H.264  ${stats.measuredKbps} kbps",
                color = Color(0xFFDCDCDC),
                fontSize = 10.sp,
            )
            val warn = stats.video.misses > 0 || stats.audio.misses > 0
            Text(
                text = "V: q=${stats.video.queue}  A: q=${stats.audio.queue}  \n" +
                       "V: d=${stats.video.drops}  A: d=${stats.audio.drops}  \n" +
                       "V: m=${stats.video.misses}  A: m=${stats.audio.misses}",
                color = if (warn) Warning else Green,
                fontSize = 10.sp,
            )
        }
    }
}
