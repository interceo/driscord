package com.driscord.presentation.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.driscord.ui.MenuBg

@Composable
internal fun RightClickMenuHost(
    modifier: Modifier = Modifier,
    menuWidth: Int = 200,
    onMenuOpened: () -> Unit = {},
    menuContent: @Composable ColumnScope.(dismiss: () -> Unit) -> Unit,
    content: @Composable BoxScope.() -> Unit,
) {
    var showMenu by remember { mutableStateOf(false) }
    var cursorPx by remember { mutableStateOf(IntOffset.Zero) }

    Box(
        modifier = modifier.pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) {
                    val event = awaitPointerEvent()
                    if (event.type == PointerEventType.Press && event.buttons.isSecondaryPressed) {
                        val pos = event.changes.first().position
                        cursorPx = IntOffset(pos.x.toInt(), pos.y.toInt())
                        onMenuOpened()
                        showMenu = true
                    }
                }
            }
        },
    ) {
        content()
        Box(modifier = Modifier.align(Alignment.TopStart).offset { cursorPx }.size(0.dp)) {
            DropdownMenu(
                expanded = showMenu,
                onDismissRequest = { showMenu = false },
                modifier = Modifier.background(MenuBg).width(menuWidth.dp),
            ) {
                menuContent { showMenu = false }
            }
        }
    }
}