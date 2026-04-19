package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Divider
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.domain.model.Server
import com.driscord.presentation.AppIntent
import com.driscord.ui.*

@Composable
fun ServerColumn(
    servers: List<Server>,
    selectedServerId: Int?,
    onIntent: (AppIntent) -> Unit,
) {
    Column(
        modifier = Modifier
            .width(64.dp)
            .fillMaxHeight()
            .background(BottomBg),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // Home / logo button
        Spacer(Modifier.height(8.dp))
        ServerIcon(
            letter = "D",
            selected = false,
            color = Blurple,
            onClick = {},
        )
        Spacer(Modifier.height(4.dp))
        Divider(color = DividerColor, thickness = 1.dp, modifier = Modifier.padding(horizontal = 12.dp))
        Spacer(Modifier.height(4.dp))

        Column(
            modifier = Modifier
                .weight(1f)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            servers.forEach { server ->
                ServerIcon(
                    letter = server.name.firstOrNull()?.uppercaseChar()?.toString() ?: "?",
                    selected = server.id == selectedServerId,
                    color = if (server.id == selectedServerId) Blurple else FieldBg,
                    tooltip = server.name,
                    onClick = { onIntent(AppIntent.SelectServer(server.id)) },
                )
                Spacer(Modifier.height(4.dp))
            }
        }

        // Add server / join by invite buttons
        Spacer(Modifier.height(4.dp))
        Divider(color = DividerColor, thickness = 1.dp, modifier = Modifier.padding(horizontal = 12.dp))
        Spacer(Modifier.height(4.dp))
        ServerIcon(
            letter = "+",
            selected = false,
            color = FieldBg,
            onClick = { onIntent(AppIntent.OpenCreateServerDialog) },
        )
        Spacer(Modifier.height(4.dp))
        ServerIcon(
            letter = "⤴",
            selected = false,
            color = FieldBg,
            onClick = { onIntent(AppIntent.OpenJoinByInviteDialog) },
        )
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun ServerIcon(
    letter: String,
    selected: Boolean,
    color: Color,
    tooltip: String = "",
    onClick: () -> Unit,
) {
    Box(
        modifier = Modifier
            .size(44.dp)
            .clip(if (selected) RoundedCornerShape(14.dp) else CircleShape)
            .background(color)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = letter,
            color = Color.White,
            fontSize = 16.sp,
            fontWeight = FontWeight.SemiBold,
        )
    }
}
