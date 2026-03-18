package com.driscord.presentation.ui.components

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
import androidx.compose.ui.unit.IntOffset
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

//import androidx.compose.foundation.background
//import androidx.compose.foundation.clickable
//import androidx.compose.foundation.layout.*
//import androidx.compose.foundation.shape.CircleShape
//import androidx.compose.foundation.shape.RoundedCornerShape
//import androidx.compose.material.*
//import androidx.compose.runtime.*
//import androidx.compose.ui.Alignment
//import androidx.compose.ui.Modifier
//import androidx.compose.ui.draw.clip
//import androidx.compose.ui.graphics.Color
//import androidx.compose.ui.input.pointer.PointerEventType
//import androidx.compose.ui.input.pointer.isSecondaryPressed
//import androidx.compose.ui.input.pointer.pointerInput
//import androidx.compose.ui.text.font.FontWeight
//import androidx.compose.ui.unit.IntOffset
//import androidx.compose.ui.unit.dp
//import androidx.compose.ui.unit.sp
//import com.driscord.ui.Blurple
//import com.driscord.ui.TextMuted