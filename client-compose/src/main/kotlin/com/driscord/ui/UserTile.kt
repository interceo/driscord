package com.driscord.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
    modifier: Modifier = Modifier
        .fillMaxWidth()
        .aspectRatio(16f / 9f),
) {
    val avatarColor = remember(peerId) {
        val h = peerId.fold(0x811c9dc5.toInt()) { acc, c -> (acc xor c.code) * 0x01000193.toInt() }
        Color(
            red   = (60 + (h and 0x7F)) / 255f,
            green = (60 + ((h shr 8) and 0x7F)) / 255f,
            blue  = (60 + ((h shr 16) and 0x7F)) / 255f,
        )
    }

    var showMenu  by remember { mutableStateOf(false) }
    var cursorPx  by remember { mutableStateOf(IntOffset.Zero) }
    var vol       by remember { mutableStateOf(onGetVolume()) }
    var peerMuted by remember { mutableStateOf(false) }
    var savedVol  by remember { mutableStateOf(1f) }

    // Avoid stale closure in pointerInput(Unit)
    val currentOnGetVolume by rememberUpdatedState(onGetVolume)

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(TileBg)
            .then(if (onClick != null) Modifier.clickable { onClick() } else Modifier)
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        if (event.type == PointerEventType.Press && event.buttons.isSecondaryPressed) {
                            val pos = event.changes.first().position
                            cursorPx = IntOffset(pos.x.toInt(), pos.y.toInt())
                            vol = currentOnGetVolume()
                            showMenu = true
                        }
                    }
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(modifier = Modifier.fillMaxSize().background(avatarColor.copy(alpha = 0.35f)))

        val avatarLetter = peerId.firstOrNull()?.uppercaseChar() ?: '?'
        Box(
            modifier = Modifier.size(48.dp).clip(CircleShape).background(avatarColor),
            contentAlignment = Alignment.Center,
        ) {
            Text(avatarLetter.toString(), color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        }

        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .fillMaxWidth()
                .background(Color(0x99000000))
                .padding(horizontal = 8.dp, vertical = 4.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(modifier = Modifier.size(7.dp).clip(CircleShape).background(if (online) Green else TextMuted))
                Spacer(Modifier.width(5.dp))
                Text(
                    label,
                    color      = Color.White,
                    fontSize   = 12.sp,
                    fontWeight = FontWeight.Medium,
                    maxLines   = 1,
                    overflow   = TextOverflow.Ellipsis,
                )
            }
        }

        if (isStreaming) {
            LiveBadge(modifier = Modifier.align(Alignment.TopEnd).padding(6.dp))
        }

        Box(modifier = Modifier.align(Alignment.TopStart).offset { cursorPx }.size(0.dp)) {
            DropdownMenu(
                expanded         = showMenu,
                onDismissRequest = { showMenu = false },
                modifier         = Modifier.background(Color(0xFF2B2D31)).width(200.dp),
            ) {
                Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
                    Text(
                        if (peerId.length > 20) peerId.take(20) + "…" else peerId,
                        color = Blurple, fontSize = 11.sp, fontWeight = FontWeight.SemiBold,
                    )
                }
                Divider(color = Color(0xFF1E1F22))
                if (isYou) {
                    VolumeSliderItem(label = "Громкость микрофона", vol = vol, onVolume = { v -> vol = v; onSetVolume(v) })
                    if (onToggleMute != null) {
                        CheckboxItem("Заглушить", muted) { onToggleMute() }
                    }
                    if (onToggleDeafen != null) {
                        CheckboxItem("Откл. звук", deafened) { onToggleDeafen() }
                    }
                } else {
                    VolumeSliderItem(label = "Громкость", vol = vol, onVolume = { v ->
                        vol = v; peerMuted = false; onSetVolume(v)
                    })
                    CheckboxItem("Заглушить", peerMuted) {
                        if (peerMuted) { vol = savedVol; onSetVolume(savedVol) }
                        else { savedVol = vol.takeIf { it > 0f } ?: 1f; vol = 0f; onSetVolume(0f) }
                        peerMuted = !peerMuted
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Shared menu helpers (internal to ui package)
// ---------------------------------------------------------------------------

@Composable
internal fun VolumeSliderItem(label: String, vol: Float, onVolume: (Float) -> Unit) {
    Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
        Column {
            Text(label, color = TextMuted, fontSize = 10.sp)
            Slider(
                value         = vol,
                onValueChange = onVolume,
                valueRange    = 0f..2f,
                colors        = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
                modifier      = Modifier.requiredWidth(176.dp).height(28.dp),
            )
        }
    }
}

@Composable
internal fun CheckboxItem(label: String, checked: Boolean, onClick: () -> Unit) {
    DropdownMenuItem(onClick = onClick) {
        Text(label, color = TextW, fontSize = 13.sp, modifier = Modifier.weight(1f))
        Checkbox(
            checked         = checked,
            onCheckedChange = null,
            colors          = CheckboxDefaults.colors(checkedColor = Blurple, uncheckedColor = TextMuted),
        )
    }
}
