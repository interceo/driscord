package com.driscord.presentation.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.ui.MenuBg
import com.driscord.ui.TextPrimary

@Composable
internal fun DropdownSelect(options: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(
            onClick = { expanded = true },
            shape = RoundedCornerShape(4.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = TextPrimary),
            modifier = Modifier.height(28.dp),
            contentPadding = PaddingValues(horizontal = 8.dp),
        ) {
            Text(options.getOrElse(selectedIndex) { "" }, fontSize = 12.sp)
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.background(MenuBg),
        ) {
            options.forEachIndexed { i, label ->
                DropdownMenuItem(onClick = { onSelect(i); expanded = false }) {
                    Text(label, color = TextPrimary, fontSize = 12.sp)
                }
            }
        }
    }
}