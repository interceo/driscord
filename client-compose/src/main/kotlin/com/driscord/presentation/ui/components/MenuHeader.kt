package com.driscord.presentation.ui.components

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.Divider
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.ui.Blurple
import com.driscord.ui.DividerColor

@Composable
internal fun MenuHeader(text: String) {
    Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
        Text(
            text = if (text.length > 20) text.take(20) + "…" else text,
            color = Blurple,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
        )
    }
    Divider(color = DividerColor)
}