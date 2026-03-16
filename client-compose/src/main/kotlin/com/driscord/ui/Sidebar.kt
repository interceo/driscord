package com.driscord.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.AppState
import com.driscord.PeerInfo

private val Blurple = Color(0xFF5865F2)
private val Green = Color(0xFF3BA55C)
private val Red = Color(0xFFED4245)
private val TextPrimary = Color(0xFFDCDDDE)
private val TextMuted = Color(0xFF72767D)
private val ButtonGray = Color(0xFF4F545C)

@Composable
fun Sidebar(
    state: AppState,
    localId: String,
    peers: List<PeerInfo>,
    muted: Boolean,
    deafened: Boolean,
    volume: Float,
    inputLevel: Float,
    outputLevel: Float,
    onConnect: (String) -> Unit,
    onDisconnect: () -> Unit,
    onToggleMute: () -> Unit,
    onToggleDeafen: () -> Unit,
    onSetVolume: (Float) -> Unit,
    onSetPeerVolume: (String, Float) -> Unit,
    onGetPeerVolume: (String) -> Float,
) {
    var serverUrl by remember { mutableStateOf("ws://localhost:8080") }
    var selectedPeer by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(12.dp)
    ) {
        Text(
            "DRISCORD",
            color = Blurple,
            fontSize = 14.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp,
        )
        Spacer(Modifier.height(8.dp))
        Divider(color = Color(0xFF40444B))
        Spacer(Modifier.height(8.dp))

        // Server input + connect button
        if (state == AppState.Disconnected) {
            Text("Server", color = TextMuted, fontSize = 11.sp)
            Spacer(Modifier.height(4.dp))
            OutlinedTextField(
                value = serverUrl,
                onValueChange = { serverUrl = it },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                colors = TextFieldDefaults.outlinedTextFieldColors(
                    textColor = TextPrimary,
                    unfocusedBorderColor = Color(0xFF40444B),
                    focusedBorderColor = Blurple,
                    backgroundColor = Color(0xFF40444B),
                ),
                textStyle = LocalTextStyle.current.copy(fontSize = 13.sp),
            )
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = { onConnect(serverUrl) },
                modifier = Modifier.fillMaxWidth().height(32.dp),
                colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                shape = RoundedCornerShape(4.dp),
            ) {
                Text("Connect", color = Color.White, fontSize = 13.sp)
            }
        }

        // Connection status
        Spacer(Modifier.height(8.dp))
        when (state) {
            AppState.Connecting ->
                Text("Connecting...", color = Color(0xFFFAA61A), fontSize = 13.sp)
            AppState.Connected -> {
                Text("Voice Connected", color = Green, fontSize = 13.sp)
                val shortId = if (localId.length > 8) localId.take(8) + "…" else localId
                Text(shortId, color = TextMuted, fontSize = 11.sp)
            }
            AppState.Disconnected ->
                Text("Not connected", color = TextMuted, fontSize = 12.sp)
        }

        Spacer(Modifier.height(12.dp))
        Divider(color = Color(0xFF40444B))
        Spacer(Modifier.height(8.dp))

        // Members list
        Text("MEMBERS", color = TextMuted, fontSize = 11.sp, letterSpacing = 0.5.sp)
        Spacer(Modifier.height(4.dp))

        if (state == AppState.Connected) {
            // Self
            val you = if (localId.length > 10) localId.take(10) + "…" else localId
            PeerRow(
                label = "$you (you)",
                color = Green,
                onClick = { selectedPeer = if (selectedPeer == localId) null else localId },
            )
            if (selectedPeer == localId) {
                VolumeRow("Mic Volume", volume, onSetVolume)
                LevelRow("Mic", inputLevel, muted)
                LevelRow("Spk", outputLevel, deafened)
            }
        }

        peers.forEach { peer ->
            val label = if (peer.id.length > 10) peer.id.take(10) + "…" else peer.id
            PeerRow(
                label = label,
                color = if (peer.connected) Green else TextMuted,
                onClick = { selectedPeer = if (selectedPeer == peer.id) null else peer.id },
            )
            if (selectedPeer == peer.id) {
                var pVol by remember(peer.id) { mutableStateOf(onGetPeerVolume(peer.id)) }
                VolumeRow("User Volume", pVol) { v ->
                    pVol = v
                    onSetPeerVolume(peer.id, v)
                }
            }
        }

        if (peers.isEmpty() && state != AppState.Connected) {
            Text("  --", color = TextMuted, fontSize = 13.sp)
        }

        // Spacer pushes voice controls to bottom
        Spacer(Modifier.weight(1f))

        if (state == AppState.Connected) {
            Divider(color = Color(0xFF40444B))
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ToggleButton("MIC", muted, Modifier.weight(1f), onToggleMute)
                ToggleButton("SND", deafened, Modifier.weight(1f), onToggleDeafen)
                Button(
                    onClick = onDisconnect,
                    modifier = Modifier.weight(1f).height(30.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Red),
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(0.dp),
                ) {
                    Text("END", color = Color.White, fontSize = 12.sp)
                }
            }
        }
    }
}

@Composable
private fun PeerRow(label: String, color: Color, onClick: () -> Unit) {
    Text(
        "  $label",
        color = color,
        fontSize = 13.sp,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 3.dp),
    )
}

@Composable
private fun VolumeRow(label: String, value: Float, onChange: (Float) -> Unit) {
    Column(modifier = Modifier.padding(start = 16.dp, bottom = 4.dp)) {
        Text(label, color = TextMuted, fontSize = 11.sp)
        Slider(
            value = value,
            onValueChange = onChange,
            valueRange = 0f..2f,
            colors = SliderDefaults.colors(thumbColor = Blurple, activeTrackColor = Blurple),
        )
    }
}

@Composable
private fun LevelRow(label: String, level: Float, active: Boolean) {
    Row(
        modifier = Modifier.padding(start = 16.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = TextMuted, fontSize = 11.sp, modifier = Modifier.width(24.dp))
        LinearProgressIndicator(
            progress = level.coerceIn(0f, 1f),
            modifier = Modifier.width(80.dp).height(8.dp),
            color = if (active) Red else Green,
            backgroundColor = Color(0xFF40444B),
        )
    }
}

@Composable
private fun ToggleButton(label: String, active: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = modifier.height(30.dp),
        colors = ButtonDefaults.buttonColors(
            backgroundColor = if (active) Red else ButtonGray
        ),
        shape = RoundedCornerShape(4.dp),
        contentPadding = PaddingValues(0.dp),
    ) {
        Text(if (active) "$label X" else label, color = Color.White, fontSize = 12.sp)
    }
}
