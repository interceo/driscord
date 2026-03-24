package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.driscord.AppConfig
import com.driscord.ui.*

@Composable
fun SettingsDialog(
    config: AppConfig,
    onDismiss: () -> Unit,
    onSave: (AppConfig) -> Unit,
) {
    var serverHost by remember { mutableStateOf(config.serverHost) }
    var serverPort by remember { mutableStateOf(config.serverPort.toString()) }
    var screenFps by remember { mutableStateOf(config.screenFps.toString()) }
    var videoBitrate by remember { mutableStateOf(config.videoBitrateKbps.toString()) }
    var gopSize by remember { mutableStateOf(config.gopSize.toString()) }
    var voiceJitter by remember { mutableStateOf(config.voiceJitterMs.toString()) }
    var screenBuffer by remember { mutableStateOf(config.screenBufferMs.toString()) }
    var maxSyncGap by remember { mutableStateOf(config.maxSyncGapMs.toString()) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.width(340.dp),
            shape = RoundedCornerShape(8.dp),
            color = VoiceBg,
            elevation = 8.dp,
        ) {
            Column(
                modifier = Modifier
                    .padding(16.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text("Settings", color = TextPrimary, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(4.dp))
                Divider(color = FieldBg)
                Spacer(Modifier.height(4.dp))

                SettingsGroup("Connection") {
                    SettingsField("Server Host", serverHost) { serverHost = it }
                    SettingsField("Server Port", serverPort) { serverPort = it }
                }

                SettingsGroup("Video") {
                    SettingsField("Capture FPS", screenFps) { screenFps = it }
                    SettingsField("Bitrate (kbps)", videoBitrate) { videoBitrate = it }
                    SettingsField("Gop size (frames)", gopSize) { gopSize = it }
                }

                SettingsGroup("Audio") {
                    SettingsField("Voice Jitter (ms)", voiceJitter) { voiceJitter = it }
                }

                SettingsGroup("A/V Sync") {
                    SettingsField("Screen Buffer (ms)", screenBuffer) { screenBuffer = it }
                    SettingsField("Max Sync Gap (ms)", maxSyncGap) { maxSyncGap = it }
                }

                Spacer(Modifier.height(2.dp))
                Text(
                    text = "* A/V Sync and Connection settings take effect after restart",
                    color = TextMuted,
                    fontSize = 10.sp,
                )
                Spacer(Modifier.height(8.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = onDismiss, modifier = Modifier.weight(1f)) {
                        Text("Cancel", color = TextMuted)
                    }
                    Button(
                        onClick = {
                            onSave(
                                config.copy(
                                    serverHost       = serverHost.ifBlank { "localhost" },
                                    serverPort       = serverPort.toIntOrNull() ?: config.serverPort,
                                    screenFps        = screenFps.toIntOrNull() ?: config.screenFps,
                                    videoBitrateKbps = videoBitrate.toIntOrNull() ?: config.videoBitrateKbps,
                                    gopSize          = gopSize.toIntOrNull() ?: config.gopSize,
                                    voiceJitterMs    = voiceJitter.toIntOrNull() ?: config.voiceJitterMs,
                                    screenBufferMs   = screenBuffer.toIntOrNull() ?: config.screenBufferMs,
                                    maxSyncGapMs     = maxSyncGap.toIntOrNull() ?: config.maxSyncGapMs,
                                ),
                            )
                            onDismiss()
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                        shape = RoundedCornerShape(4.dp),
                    ) {
                        Text("Save", color = Color.White)
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingsGroup(
    title: String,
    content: @Composable ColumnScope.() -> Unit,
) {
    Text(title, color = TextMuted, fontSize = 10.sp, letterSpacing = 0.5.sp)
    Spacer(Modifier.height(2.dp))
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(FieldBg.copy(alpha = 0.5f), RoundedCornerShape(4.dp))
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
        content = content,
    )
    Spacer(Modifier.height(6.dp))
}

@Composable
private fun SettingsField(
    label: String,
    value: String,
    onChange: (String) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = TextPrimary, fontSize = 12.sp, modifier = Modifier.weight(1f).padding(top = 6.dp))
        OutlinedTextField(
            value = value,
            onValueChange = onChange,
            singleLine = true,
            modifier = Modifier.width(110.dp),
            colors = TextFieldDefaults.outlinedTextFieldColors(
                textColor = TextPrimary,
                unfocusedBorderColor = FieldBg,
                focusedBorderColor = Blurple,
                backgroundColor = FieldBgDark,
                cursorColor = Blurple,
            ),
            textStyle = LocalTextStyle.current.copy(fontSize = 12.sp),
        )
    }
}
