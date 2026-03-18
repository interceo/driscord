package com.driscord.presentation.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
internal fun AvatarBox(
    peerId: String,
    size: Int = 28,
    fontSize: Int = 12,
    modifier: Modifier = Modifier,
) {
    val color = remember(peerId) { peerAvatarColor(peerId) }
    Box(
        modifier = modifier.size(size.dp).clip(CircleShape).background(color),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = (peerId.firstOrNull()?.uppercaseChar() ?: '?').toString(),
            color = Color.White,
            fontSize = fontSize.sp,
            fontWeight = FontWeight.Bold,
        )
    }
}

internal fun peerAvatarColor(peerId: String): Color {
    val h = peerId.fold(0x811c9dc5.toInt()) { acc, c -> (acc xor c.code) * 0x01000193 }
    return Color(
        red   = (60 + (h          and 0x7F)) / 255f,
        green = (60 + ((h shr  8) and 0x7F)) / 255f,
        blue  = (60 + ((h shr 16) and 0x7F)) / 255f,
    )
}