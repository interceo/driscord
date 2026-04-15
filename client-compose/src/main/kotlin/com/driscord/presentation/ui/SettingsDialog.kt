package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.driscord.AppConfig
import com.driscord.data.audio.AudioDevice
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.ui.Blurple
import com.driscord.ui.FieldBg
import com.driscord.ui.FieldBgDark
import com.driscord.ui.TextMuted
import com.driscord.ui.TextPrimary
import com.driscord.ui.VoiceBg
import org.jetbrains.compose.resources.stringResource

@Composable
fun SettingsDialog(
    config: AppConfig,
    onDismiss: () -> Unit,
    onSave: (AppConfig) -> Unit,
    onListInputDevices: () -> List<AudioDevice>,
    onListOutputDevices: () -> List<AudioDevice>,
) {
    var serverHost by remember { mutableStateOf(config.serverHost) }
    var serverPort by remember { mutableStateOf(config.serverPort.toString()) }
    var screenFps by remember { mutableStateOf(config.screenFps.toString()) }

    // Load device lists once on open; prepend "Default" (empty id = system default)
    val defaultLabel = stringResource(Res.string.default_device)
    val inputOptions: List<AudioDevice> = remember {
        listOf(AudioDevice("", defaultLabel)) + onListInputDevices()
    }
    val outputOptions: List<AudioDevice> = remember {
        listOf(AudioDevice("", defaultLabel)) + onListOutputDevices()
    }
    var selectedInputIdx by remember(config.micDeviceId, inputOptions) {
        mutableStateOf(inputOptions.indexOfFirst { it.id == config.micDeviceId }.coerceAtLeast(0))
    }
    var selectedOutputIdx by remember(config.outputDeviceId, outputOptions) {
        mutableStateOf(outputOptions.indexOfFirst { it.id == config.outputDeviceId }.coerceAtLeast(0))
    }
    var inputMenuExpanded by remember { mutableStateOf(false) }
    var outputMenuExpanded by remember { mutableStateOf(false) }

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
                Text(stringResource(Res.string.settings), color = TextPrimary, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(4.dp))
                Divider(color = FieldBg)
                Spacer(Modifier.height(4.dp))

                SettingsGroup(stringResource(Res.string.audio)) {
                    SettingsDropdown(
                        label = stringResource(Res.string.microphone),
                        options = inputOptions.map { it.name },
                        selectedIndex = selectedInputIdx,
                        expanded = inputMenuExpanded,
                        onExpandedChange = { inputMenuExpanded = it },
                        onSelect = { selectedInputIdx = it; inputMenuExpanded = false },
                    )
                    SettingsDropdown(
                        label = stringResource(Res.string.output),
                        options = outputOptions.map { it.name },
                        selectedIndex = selectedOutputIdx,
                        expanded = outputMenuExpanded,
                        onExpandedChange = { outputMenuExpanded = it },
                        onSelect = { selectedOutputIdx = it; outputMenuExpanded = false },
                    )
                }

                SettingsGroup(stringResource(Res.string.connection)) {
                    SettingsField(stringResource(Res.string.server_host), serverHost) { serverHost = it }
                    SettingsField(stringResource(Res.string.server_port), serverPort) { serverPort = it }
                }

                SettingsGroup(stringResource(Res.string.video)) {
                    SettingsField(stringResource(Res.string.capture_fps), screenFps) { screenFps = it }
                }

                Spacer(Modifier.height(2.dp))
                Text(
                    text = stringResource(Res.string.settings_restart_note),
                    color = TextMuted,
                    fontSize = 10.sp,
                )
                Spacer(Modifier.height(8.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = onDismiss, modifier = Modifier.weight(1f)) {
                        Text(stringResource(Res.string.cancel), color = TextMuted)
                    }
                    Button(
                        onClick = {
                            onSave(
                                config.copy(
                                    serverHost     = serverHost.ifBlank { "localhost" },
                                    serverPort     = serverPort.toIntOrNull() ?: config.serverPort,
                                    screenFps      = screenFps.toIntOrNull() ?: config.screenFps,
                                    micDeviceId    = inputOptions.getOrNull(selectedInputIdx)?.id ?: "",
                                    outputDeviceId = outputOptions.getOrNull(selectedOutputIdx)?.id ?: "",
                                ),
                            )
                            onDismiss()
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                        shape = RoundedCornerShape(4.dp),
                    ) {
                        Text(stringResource(Res.string.save), color = Color.White)
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
private fun SettingsDropdown(
    label: String,
    options: List<String>,
    selectedIndex: Int,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    onSelect: (Int) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = TextPrimary, fontSize = 12.sp, modifier = Modifier.weight(1f))
        Box {
            OutlinedButton(
                onClick = { onExpandedChange(true) },
                shape = RoundedCornerShape(4.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = TextPrimary),
                modifier = Modifier.width(110.dp).height(32.dp),
                contentPadding = PaddingValues(horizontal = 8.dp),
            ) {
                Text(
                    text = options.getOrElse(selectedIndex) { "" },
                    fontSize = 11.sp,
                    maxLines = 1,
                    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                )
            }
            DropdownMenu(
                expanded = expanded,
                onDismissRequest = { onExpandedChange(false) },
                modifier = Modifier.background(FieldBgDark),
            ) {
                options.forEachIndexed { i, name ->
                    DropdownMenuItem(onClick = { onSelect(i) }) {
                        Text(name, color = TextPrimary, fontSize = 12.sp)
                    }
                }
            }
        }
    }
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
