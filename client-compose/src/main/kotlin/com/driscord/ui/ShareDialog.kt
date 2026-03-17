package com.driscord.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.driscord.CaptureTarget

@Composable
fun ShareDialog(
    systemAudioAvailable: Boolean,
    onListTargets: () -> List<CaptureTarget>,
    onGrabThumbnail: (CaptureTarget) -> ImageBitmap?,
    onDismiss: () -> Unit,
    onGo: (CaptureTarget, Int, Int, Boolean) -> Unit,
) {
    val targets    = remember { onListTargets() }
    val thumbnails = remember {
        mutableStateMapOf<Int, ImageBitmap?>().also { map ->
            targets.forEachIndexed { i, t -> map[i] = onGrabThumbnail(t) }
        }
    }

    var selectedIdx by remember { mutableStateOf(-1) }
    var quality     by remember { mutableStateOf(2) }
    var fps         by remember { mutableStateOf(30) }
    var shareAudio  by remember { mutableStateOf(true) }

    val qualities  = listOf("Source", "720p", "1080p", "1440p")
    val fpsOptions = listOf(15, 30, 60)

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier  = Modifier.size(700.dp, 520.dp),
            shape     = RoundedCornerShape(8.dp),
            color     = Color(0xFF2B2D31),
            elevation = 8.dp,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Choose what to share", color = TextW, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(8.dp))
                Divider(color = Color(0xFF1E1F22))
                Spacer(Modifier.height(8.dp))

                LazyVerticalGrid(
                    columns = GridCells.Adaptive(160.dp),
                    modifier = Modifier.weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(targets.indices.toList()) { idx ->
                        val selected = idx == selectedIdx
                        Column(
                            modifier = Modifier
                                .clip(RoundedCornerShape(4.dp))
                                .border(2.dp, if (selected) Blurple else Color.Transparent, RoundedCornerShape(4.dp))
                                .clickable { selectedIdx = idx }
                                .padding(4.dp),
                        ) {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .aspectRatio(16f / 9f)
                                    .clip(RoundedCornerShape(3.dp))
                                    .background(Color(0xFF111214)),
                                contentAlignment = Alignment.Center,
                            ) {
                                val thumb = thumbnails[idx]
                                if (thumb != null) {
                                    Image(
                                        bitmap             = thumb,
                                        contentDescription = null,
                                        modifier           = Modifier.fillMaxSize(),
                                        contentScale       = ContentScale.Fit,
                                    )
                                } else {
                                    Text("📺", fontSize = 20.sp)
                                }
                            }
                            Text(
                                targets[idx].name.take(24),
                                color    = TextW,
                                fontSize = 11.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(top = 2.dp),
                            )
                        }
                    }
                }

                Spacer(Modifier.height(8.dp))
                Divider(color = Color(0xFF1E1F22))
                Spacer(Modifier.height(8.dp))

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    Column {
                        Text("Quality", color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(qualities, quality) { quality = it }
                    }
                    Column {
                        Text("FPS", color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(fpsOptions.map { "$it" }, fpsOptions.indexOf(fps)) { fps = fpsOptions[it] }
                    }
                    if (systemAudioAvailable) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked         = shareAudio,
                                onCheckedChange = { shareAudio = it },
                                colors          = CheckboxDefaults.colors(checkedColor = Blurple),
                            )
                            Text("Share Audio", color = TextW, fontSize = 13.sp)
                        }
                    }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onDismiss) { Text("Cancel", color = TextMuted) }
                    Button(
                        onClick  = { if (selectedIdx >= 0) onGo(targets[selectedIdx], quality, fps, shareAudio) },
                        enabled  = selectedIdx >= 0,
                        colors   = ButtonDefaults.buttonColors(backgroundColor = Green),
                        shape    = RoundedCornerShape(4.dp),
                    ) {
                        Text("Go Live", color = Color.White)
                    }
                }
            }
        }
    }
}

@Composable
fun DropdownSelect(options: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(
            onClick         = { expanded = true },
            shape           = RoundedCornerShape(4.dp),
            colors          = ButtonDefaults.outlinedButtonColors(contentColor = TextW),
            modifier        = Modifier.height(28.dp),
            contentPadding  = PaddingValues(horizontal = 8.dp),
        ) {
            Text(options.getOrElse(selectedIndex) { "" }, fontSize = 12.sp)
        }
        DropdownMenu(
            expanded         = expanded,
            onDismissRequest = { expanded = false },
            modifier         = Modifier.background(Color(0xFF2B2D31)),
        ) {
            options.forEachIndexed { i, label ->
                DropdownMenuItem(onClick = { onSelect(i); expanded = false }) {
                    Text(label, color = TextW, fontSize = 12.sp)
                }
            }
        }
    }
}
