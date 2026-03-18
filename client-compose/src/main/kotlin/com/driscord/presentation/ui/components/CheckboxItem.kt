package com.driscord.presentation.ui.components

import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.sp
import com.driscord.ui.Blurple
import com.driscord.ui.TextMuted
import com.driscord.ui.TextPrimary

@Composable
internal fun CheckboxItem(label: String, checked: Boolean, onClick: () -> Unit) {
    DropdownMenuItem(onClick = onClick) {
        Text(label, color = TextPrimary, fontSize = 13.sp, modifier = Modifier.weight(1f))
        Checkbox(
            checked = checked,
            onCheckedChange = null,
            colors = CheckboxDefaults.colors(checkedColor = Blurple, uncheckedColor = TextMuted),
        )
    }
}
