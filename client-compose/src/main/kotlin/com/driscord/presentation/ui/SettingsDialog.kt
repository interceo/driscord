package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.driscord.AppConfig
import com.driscord.data.audio.AudioDevice
import com.driscord.domain.model.UserProfile
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.presentation.SettingsPage
import com.driscord.presentation.ui.components.AvatarBox
import com.driscord.ui.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.jetbrains.compose.resources.stringResource

@Composable
fun SettingsDialog(
    config: AppConfig,
    userProfile: UserProfile?,
    profileSaving: Boolean,
    onDismiss: () -> Unit,
    onSave: (AppConfig) -> Unit,
    onUpdateDisplayName: (String) -> Unit,
    onUploadAvatar: (ByteArray, String) -> Unit,
    onListInputDevices: () -> List<AudioDevice>,
    onListOutputDevices: () -> List<AudioDevice>,
) {
    var currentPage by remember { mutableStateOf(SettingsPage.MyAccount) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.width(820.dp).height(540.dp),
            shape = RoundedCornerShape(8.dp),
            color = ContentBg,
            elevation = 16.dp,
        ) {
            Row(modifier = Modifier.fillMaxSize()) {
                // ── Left navigation panel ──────────────────────────────────
                NavPanel(
                    userProfile = userProfile,
                    currentPage = currentPage,
                    onNavigate = { currentPage = it },
                    onDismiss = onDismiss,
                )
                Divider(
                    color = DividerColor,
                    modifier = Modifier.fillMaxHeight().width(1.dp),
                )
                // ── Right content panel ────────────────────────────────────
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(ContentBg)
                        .padding(horizontal = 24.dp, vertical = 20.dp),
                ) {
                    when (currentPage) {
                        SettingsPage.MyAccount -> MyAccountPage(
                            userProfile = userProfile,
                            profileSaving = profileSaving,
                            onUpdateDisplayName = onUpdateDisplayName,
                            onUploadAvatar = onUploadAvatar,
                        )
                        SettingsPage.Audio -> AudioPage(
                            config = config,
                            onSave = onSave,
                            onListInputDevices = onListInputDevices,
                            onListOutputDevices = onListOutputDevices,
                        )
                        SettingsPage.Advanced -> AdvancedPage(
                            config = config,
                            onSave = onSave,
                        )
                    }
                }
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Left nav panel
// ────────────────────────────────────────────────────────────────────────────

@Composable
private fun NavPanel(
    userProfile: UserProfile?,
    currentPage: SettingsPage,
    onNavigate: (SettingsPage) -> Unit,
    onDismiss: () -> Unit,
) {
    Column(
        modifier = Modifier
            .width(220.dp)
            .fillMaxHeight()
            .background(VoiceBg)
            .padding(horizontal = 8.dp, vertical = 12.dp),
    ) {
        // User mini-header
        if (userProfile != null) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(6.dp))
                    .background(FieldBg.copy(alpha = 0.35f))
                    .padding(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                AvatarBox(
                    peerId = userProfile.username,
                    size = 36,
                    fontSize = 15,
                    avatarUrl = userProfile.avatarUrl,
                )
                Spacer(Modifier.width(8.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = userProfile.displayName ?: userProfile.username,
                        color = TextPrimary,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                    )
                    Text(
                        text = "@${userProfile.username}",
                        color = TextMuted,
                        fontSize = 10.sp,
                        maxLines = 1,
                        overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                    )
                }
            }
            Spacer(Modifier.height(10.dp))
        }

        NavSectionLabel(stringResource(Res.string.user_settings))
        NavItem(
            label = stringResource(Res.string.my_account),
            selected = currentPage == SettingsPage.MyAccount,
            onClick = { onNavigate(SettingsPage.MyAccount) },
        )

        Spacer(Modifier.height(6.dp))
        Divider(color = DividerColor.copy(alpha = 0.5f))
        Spacer(Modifier.height(6.dp))

        NavSectionLabel(stringResource(Res.string.app_settings))
        NavItem(
            label = stringResource(Res.string.audio),
            selected = currentPage == SettingsPage.Audio,
            onClick = { onNavigate(SettingsPage.Audio) },
        )
        NavItem(
            label = stringResource(Res.string.advanced),
            selected = currentPage == SettingsPage.Advanced,
            onClick = { onNavigate(SettingsPage.Advanced) },
        )

        Spacer(Modifier.weight(1f))
        Divider(color = DividerColor.copy(alpha = 0.5f))
        Spacer(Modifier.height(6.dp))
        // Close button
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(4.dp))
                .clickable(onClick = onDismiss)
                .padding(horizontal = 8.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("✕", color = TextMuted, fontSize = 12.sp, modifier = Modifier.width(18.dp))
            Spacer(Modifier.width(6.dp))
            Text(stringResource(Res.string.cancel), color = TextMuted, fontSize = 13.sp)
        }
    }
}

@Composable
private fun NavSectionLabel(text: String) {
    Text(
        text = text.uppercase(),
        color = TextMuted,
        fontSize = 10.sp,
        letterSpacing = 0.6.sp,
        modifier = Modifier.padding(start = 8.dp, bottom = 3.dp, top = 2.dp),
    )
}

@Composable
private fun NavItem(label: String, selected: Boolean, onClick: () -> Unit) {
    val bg = if (selected) Blurple.copy(alpha = 0.22f) else Color.Transparent
    val textColor = if (selected) TextPrimary else TextMuted
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(4.dp))
            .background(bg)
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            color = textColor,
            fontSize = 13.sp,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
        )
    }
}

// ────────────────────────────────────────────────────────────────────────────
// My Account page
// ────────────────────────────────────────────────────────────────────────────

@Composable
private fun MyAccountPage(
    userProfile: UserProfile?,
    profileSaving: Boolean,
    onUpdateDisplayName: (String) -> Unit,
    onUploadAvatar: (ByteArray, String) -> Unit,
) {
    var pickingFile    by remember { mutableStateOf(false) }
    var cropImageBytes by remember { mutableStateOf<ByteArray?>(null) }
    var editingDisplayName by remember { mutableStateOf(false) }
    var displayNameInput by remember(userProfile?.displayName) {
        mutableStateOf(userProfile?.displayName ?: "")
    }

    // Step 1: open OS file picker on IO thread
    if (pickingFile) {
        LaunchedEffect(Unit) {
            val bytes = withContext(Dispatchers.IO) {
                val dialog = java.awt.FileDialog(
                    null as java.awt.Frame?,
                    "Choose avatar",
                    java.awt.FileDialog.LOAD,
                )
                dialog.setFilenameFilter { _, name ->
                    name.lowercase().let {
                        it.endsWith(".jpg") || it.endsWith(".jpeg") ||
                        it.endsWith(".png") || it.endsWith(".webp")
                    }
                }
                dialog.isVisible = true
                val filename = dialog.file
                val dir      = dialog.directory
                if (filename != null && dir != null) java.io.File(dir, filename).readBytes()
                else null
            }
            if (bytes != null) cropImageBytes = bytes
            pickingFile = false
        }
    }

    // Step 2: show crop dialog when image bytes are ready
    cropImageBytes?.let { bytes ->
        AvatarCropDialog(
            imageBytes = bytes,
            onConfirm  = { croppedPng ->
                onUploadAvatar(croppedPng, "png")
                cropImageBytes = null
            },
            onDismiss  = { cropImageBytes = null },
        )
    }

    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(0.dp),
    ) {
        Text(
            stringResource(Res.string.my_account),
            color = TextPrimary,
            fontSize = 17.sp,
            fontWeight = FontWeight.Bold,
        )
        Spacer(Modifier.height(16.dp))

        // Profile card
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(VoiceBg)
                .padding(16.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                // Avatar — clickable to upload
                Box {
                    AvatarBox(
                        peerId = userProfile?.username ?: "?",
                        size = 64,
                        fontSize = 26,
                        avatarUrl = userProfile?.avatarUrl,
                        onClick = if (!profileSaving) {{ pickingFile = true }} else null,
                    )
                    // Camera hint overlay when hovered isn't possible in Material2
                    // but a small badge signals clickability
                    Box(
                        modifier = Modifier
                            .size(20.dp)
                            .clip(RoundedCornerShape(10.dp))
                            .background(FieldBgDark)
                            .align(Alignment.BottomEnd),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text("✎", color = TextMuted, fontSize = 9.sp)
                    }
                }
                Spacer(Modifier.width(16.dp))
                Column {
                    Text(
                        text = userProfile?.displayName ?: userProfile?.username ?: "—",
                        color = TextPrimary,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold,
                    )
                    Text(
                        text = "@${userProfile?.username ?: "—"}",
                        color = TextMuted,
                        fontSize = 12.sp,
                    )
                    if (profileSaving) {
                        Spacer(Modifier.height(4.dp))
                        CircularProgressIndicator(
                            modifier = Modifier.size(14.dp),
                            strokeWidth = 2.dp,
                            color = Blurple,
                        )
                    }
                }
            }
        }

        Spacer(Modifier.height(16.dp))
        Divider(color = FieldBg)
        Spacer(Modifier.height(16.dp))

        // Display name row
        ProfileRow(
            label = stringResource(Res.string.display_name),
            value = userProfile?.displayName?.ifBlank { null }
                ?: stringResource(Res.string.display_name_placeholder),
            valueAlpha = if (userProfile?.displayName.isNullOrBlank()) 0.45f else 1f,
            editing = editingDisplayName,
            editValue = displayNameInput,
            onEditValueChange = { displayNameInput = it },
            onClickChange = { editingDisplayName = true },
            onSave = {
                onUpdateDisplayName(displayNameInput)
                editingDisplayName = false
            },
            onCancel = {
                displayNameInput = userProfile?.displayName ?: ""
                editingDisplayName = false
            },
            saveEnabled = !profileSaving,
        )

        Spacer(Modifier.height(12.dp))

        // Username row (read-only)
        ProfileRowReadOnly(
            label = stringResource(Res.string.username),
            value = userProfile?.username ?: "—",
        )

        Spacer(Modifier.height(12.dp))

        // Email row (masked)
        val maskedEmail = userProfile?.email?.let { email ->
            val at = email.indexOf('@')
            if (at > 1) email.first() + "***" + email.substring(at) else "***"
        } ?: "—"
        ProfileRowReadOnly(
            label = stringResource(Res.string.email),
            value = maskedEmail,
        )
    }
}

@Composable
private fun ProfileRow(
    label: String,
    value: String,
    valueAlpha: Float = 1f,
    editing: Boolean,
    editValue: String,
    onEditValueChange: (String) -> Unit,
    onClickChange: () -> Unit,
    onSave: () -> Unit,
    onCancel: () -> Unit,
    saveEnabled: Boolean,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(6.dp))
            .background(VoiceBg)
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        Text(label, color = TextMuted, fontSize = 10.sp, letterSpacing = 0.4.sp)
        Spacer(Modifier.height(4.dp))
        if (editing) {
            OutlinedTextField(
                value = editValue,
                onValueChange = onEditValueChange,
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                colors = TextFieldDefaults.outlinedTextFieldColors(
                    textColor = TextPrimary,
                    unfocusedBorderColor = FieldBg,
                    focusedBorderColor = Blurple,
                    backgroundColor = FieldBgDark,
                    cursorColor = Blurple,
                ),
                textStyle = LocalTextStyle.current.copy(fontSize = 13.sp),
            )
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = onCancel) {
                    Text(stringResource(Res.string.cancel), color = TextMuted, fontSize = 12.sp)
                }
                Button(
                    onClick = onSave,
                    enabled = saveEnabled,
                    colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                    shape = RoundedCornerShape(4.dp),
                ) {
                    Text(stringResource(Res.string.save_changes), color = Color.White, fontSize = 12.sp)
                }
            }
        } else {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = value,
                    color = TextPrimary.copy(alpha = valueAlpha),
                    fontSize = 13.sp,
                    modifier = Modifier.weight(1f),
                )
                OutlinedButton(
                    onClick = onClickChange,
                    shape = RoundedCornerShape(4.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = TextPrimary),
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp),
                    modifier = Modifier.height(28.dp),
                ) {
                    Text(stringResource(Res.string.change), fontSize = 11.sp)
                }
            }
        }
    }
}

@Composable
private fun ProfileRowReadOnly(label: String, value: String) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(6.dp))
            .background(VoiceBg)
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        Text(label, color = TextMuted, fontSize = 10.sp, letterSpacing = 0.4.sp)
        Spacer(Modifier.height(4.dp))
        Text(value, color = TextPrimary, fontSize = 13.sp)
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Audio page
// ────────────────────────────────────────────────────────────────────────────

@Composable
private fun AudioPage(
    config: AppConfig,
    onSave: (AppConfig) -> Unit,
    onListInputDevices: () -> List<AudioDevice>,
    onListOutputDevices: () -> List<AudioDevice>,
) {
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

    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        PageTitle(stringResource(Res.string.audio))
        Spacer(Modifier.height(12.dp))

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

        Spacer(Modifier.weight(1f))
        SaveBar(onSave = {
            onSave(
                config.copy(
                    micDeviceId    = inputOptions.getOrNull(selectedInputIdx)?.id ?: "",
                    outputDeviceId = outputOptions.getOrNull(selectedOutputIdx)?.id ?: "",
                )
            )
        })
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Advanced page
// ────────────────────────────────────────────────────────────────────────────

@Composable
private fun AdvancedPage(config: AppConfig, onSave: (AppConfig) -> Unit) {
    var server by remember { mutableStateOf(config.server) }
    var api by remember { mutableStateOf(config.api) }
    var screenFps by remember { mutableStateOf(config.screenFps.toString()) }

    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        PageTitle(stringResource(Res.string.advanced))
        Spacer(Modifier.height(12.dp))

        SettingsGroup(stringResource(Res.string.connection)) {
            SettingsField(stringResource(Res.string.server_address), server) { server = it }
            SettingsField(stringResource(Res.string.api_address), api) { api = it }
        }

        SettingsGroup(stringResource(Res.string.video)) {
            SettingsField(stringResource(Res.string.capture_fps), screenFps) { screenFps = it }
        }

        Text(
            text = stringResource(Res.string.settings_restart_note),
            color = TextMuted,
            fontSize = 10.sp,
        )
        Spacer(Modifier.weight(1f))
        SaveBar(onSave = {
            onSave(
                config.copy(
                    server    = server.ifBlank { "localhost:9001" },
                    api       = api.ifBlank { "localhost:9002" },
                    screenFps = screenFps.toIntOrNull() ?: config.screenFps,
                )
            )
        })
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ────────────────────────────────────────────────────────────────────────────

@Composable
private fun PageTitle(text: String) {
    Text(text, color = TextPrimary, fontSize = 17.sp, fontWeight = FontWeight.Bold)
    Spacer(Modifier.height(4.dp))
    Divider(color = FieldBg)
}

@Composable
private fun SaveBar(onSave: () -> Unit) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
        Button(
            onClick = onSave,
            colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
            shape = RoundedCornerShape(4.dp),
        ) {
            Text(stringResource(Res.string.save), color = Color.White)
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
                modifier = Modifier.width(150.dp).height(32.dp),
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
        Text(
            label,
            color = TextPrimary,
            fontSize = 12.sp,
            modifier = Modifier.weight(1f).padding(top = 6.dp),
        )
        OutlinedTextField(
            value = value,
            onValueChange = onChange,
            singleLine = true,
            modifier = Modifier.width(160.dp),
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
