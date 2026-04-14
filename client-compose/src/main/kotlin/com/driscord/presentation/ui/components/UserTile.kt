package com.driscord.presentation.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.ui.Green
import com.driscord.ui.TextMuted
import com.driscord.ui.TileBg
import com.driscord.ui.TileOverlay
import org.jetbrains.compose.resources.stringResource

@Composable
internal fun UserTile(
    peerId: String,
    label: String,
    online: Boolean,
    isStreaming: Boolean,
    isYou: Boolean = false,
    muted: Boolean = false,
    deafened: Boolean = false,
    onGetVolume: () -> Float = { 1f },
    onSetVolume: (Float) -> Unit = {},
    onToggleMute: (() -> Unit)? = null,
    onToggleDeafen: (() -> Unit)? = null,
    onClick: (() -> Unit)? = null,
    modifier: Modifier = Modifier,
) {
    val avatarColor = remember(peerId) { peerAvatarColor(peerId) }

    var vol by remember { mutableStateOf(onGetVolume()) }
    var peerMuted by remember { mutableStateOf(false) }
    var savedVol by remember { mutableStateOf(1f) }
    val currentOnGetVolume by rememberUpdatedState(onGetVolume)

    RightClickMenuHost(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(TileBg)
            .then(if (onClick != null) Modifier.clickable { onClick() } else Modifier),
        onMenuOpened = {
            val current = currentOnGetVolume()
            vol = current
            peerMuted = current == 0f
        },
        menuContent = { _ ->
            MenuHeader(peerId)
            if (isYou) {
                VolumeSliderItem(label = stringResource(Res.string.mic_volume), vol = vol, onVolume = { v ->
                    vol = v
                    onSetVolume(v)
                })
                if (onToggleMute != null) CheckboxItem(stringResource(Res.string.mute), muted) { onToggleMute() }
                if (onToggleDeafen != null) CheckboxItem(stringResource(Res.string.mute_sound), deafened) { onToggleDeafen() }
            } else {
                VolumeSliderItem(label = stringResource(Res.string.volume), vol = vol, onVolume = { v ->
                    vol = v
                    peerMuted = false
                    onSetVolume(v)
                })
                CheckboxItem(stringResource(Res.string.mute), peerMuted) {
                    if (peerMuted) {
                        vol = savedVol
                        onSetVolume(savedVol)
                    } else {
                        savedVol = vol.takeIf { it > 0f } ?: 1f
                        vol = 0f
                        onSetVolume(0f)
                    }
                    peerMuted = !peerMuted
                }
            }
        },
    ) {
        Box(modifier = Modifier.fillMaxSize().background(avatarColor.copy(alpha = 0.35f)))

        AvatarBox(
            peerId = peerId,
            size = 48,
            fontSize = 22,
            modifier = Modifier.align(Alignment.Center),
        )

        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .fillMaxWidth()
                .background(TileOverlay)
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
                    text = label,
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
