package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Divider
import androidx.compose.material.DropdownMenu
import androidx.compose.material.DropdownMenuItem
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.domain.model.Server
import com.driscord.driscord_compose.generated.resources.Res
import com.driscord.driscord_compose.generated.resources.create_server
import com.driscord.driscord_compose.generated.resources.join_by_invite
import com.driscord.presentation.AppIntent
import com.driscord.ui.*
import org.jetbrains.compose.resources.stringResource

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

        // Add server menu (+ button with dropdown: create / join by invite)
        Spacer(Modifier.height(4.dp))
        Divider(color = DividerColor, thickness = 1.dp, modifier = Modifier.padding(horizontal = 12.dp))
        Spacer(Modifier.height(4.dp))
        AddServerMenu(onIntent = onIntent)
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun AddServerMenu(onIntent: (AppIntent) -> Unit) {
    var expanded by remember { mutableStateOf(false) }

    Box {
        ServerIcon(
            letter = "+",
            selected = false,
            color = FieldBg,
            onClick = { expanded = true },
        )
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.background(SidebarBg),
        ) {
            DropdownMenuItem(onClick = {
                expanded = false
                onIntent(AppIntent.OpenCreateServerDialog)
            }) {
                Text(stringResource(Res.string.create_server), color = TextPrimary, fontSize = 12.sp)
            }
            DropdownMenuItem(onClick = {
                expanded = false
                onIntent(AppIntent.OpenJoinByInviteDialog)
            }) {
                Text(stringResource(Res.string.join_by_invite), color = TextPrimary, fontSize = 12.sp)
            }
        }
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
