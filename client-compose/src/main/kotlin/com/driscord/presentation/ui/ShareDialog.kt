package com.driscord.presentation.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.Button
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.Checkbox
import androidx.compose.material.CheckboxDefaults
import androidx.compose.material.Divider
import androidx.compose.material.Surface
import androidx.compose.material.Text
import androidx.compose.material.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
import com.driscord.domain.model.CaptureTarget
import com.driscord.presentation.ui.components.DropdownSelect
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.ui.Blurple
import com.driscord.ui.DividerColor
import com.driscord.ui.Green
import com.driscord.ui.SidebarBg
import com.driscord.ui.TextMuted
import com.driscord.ui.TextPrimary
import org.jetbrains.compose.resources.stringResource

@Composable
fun ShareDialog(
    systemAudioAvailable: Boolean,
    onListTargets: () -> List<CaptureTarget>,
    onGrabThumbnail: (CaptureTarget) -> ImageBitmap?,
    onDismiss: () -> Unit,
    onGo: (CaptureTarget, Int, Int, Boolean) -> Unit,
) {
    val targets = remember { onListTargets() }
    val thumbnails = remember {
        mutableMapOf<Int, ImageBitmap?>().also { map ->
            targets.forEachIndexed { i, t -> map[i] = onGrabThumbnail(t) }
        }
    }

    var selectedIdx by remember { mutableStateOf(-1) }
    var quality by remember { mutableStateOf(2) }
    var fps by remember { mutableStateOf(30) }
    var shareAudio by remember { mutableStateOf(true) }

    val qualities = listOf("Source", "720p", "1080p", "1440p")
    val fpsOptions = listOf(15, 30, 60)

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.size(700.dp, 520.dp),
            shape = RoundedCornerShape(8.dp),
            color = SidebarBg,
            elevation = 8.dp,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = stringResource(Res.string.choose_what_to_share),
                    color = TextPrimary,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.SemiBold,
                )
                Spacer(Modifier.height(8.dp))
                Divider(color = DividerColor)
                Spacer(Modifier.height(8.dp))

                LazyVerticalGrid(
                    columns = GridCells.Adaptive(160.dp),
                    modifier = Modifier.weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    itemsIndexed(targets) { idx, target ->
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
                                        bitmap = thumb,
                                        contentDescription = null,
                                        modifier = Modifier.fillMaxSize(),
                                        contentScale = ContentScale.Fit,
                                    )
                                } else {
                                    Text("📺", fontSize = 20.sp)
                                }
                            }
                            Text(
                                text = target.name.take(24),
                                color = TextPrimary,
                                fontSize = 11.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(top = 2.dp),
                            )
                        }
                    }
                }

                Spacer(Modifier.height(8.dp))
                Divider(color = DividerColor)
                Spacer(Modifier.height(8.dp))

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    Column {
                        Text(stringResource(Res.string.quality), color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(qualities, quality) { quality = it }
                    }
                    Column {
                        Text(stringResource(Res.string.fps), color = TextMuted, fontSize = 11.sp)
                        DropdownSelect(
                            options = fpsOptions.map { "$it" },
                            selectedIndex = fpsOptions.indexOf(fps),
                            onSelect = { fps = fpsOptions[it] },
                        )
                    }
                    if (systemAudioAvailable) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = shareAudio,
                                onCheckedChange = { shareAudio = it },
                                colors = CheckboxDefaults.colors(checkedColor = Blurple),
                            )
                            Text(stringResource(Res.string.share_audio), color = TextPrimary, fontSize = 13.sp)
                        }
                    }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onDismiss) {
                        Text(stringResource(Res.string.cancel), color = TextMuted)
                    }
                    Button(
                        onClick = { if (selectedIdx >= 0) onGo(targets[selectedIdx], quality, fps, shareAudio) },
                        enabled = selectedIdx >= 0,
                        colors = ButtonDefaults.buttonColors(backgroundColor = Green),
                        shape = RoundedCornerShape(4.dp),
                    ) {
                        Text(stringResource(Res.string.go_live), color = Color.White)
                    }
                }
            }
        }
    }
}
