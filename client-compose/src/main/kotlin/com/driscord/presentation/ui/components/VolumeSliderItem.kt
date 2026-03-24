package com.driscord.presentation.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.material.Slider
import androidx.compose.material.SliderDefaults
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.ui.Blurple
import com.driscord.ui.TextMuted

@Composable
internal fun VolumeSliderItem(label: String, vol: Float, onVolume: (Float) -> Unit) {
    Box(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
        Column {
            Text(label, color = TextMuted, fontSize = 10.sp)
            Slider(
                value = vol,
                onValueChange = onVolume,
                valueRange = 0f..2f,
                colors = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
                modifier = Modifier.requiredWidth(176.dp).height(28.dp),
            )
        }
    }
}