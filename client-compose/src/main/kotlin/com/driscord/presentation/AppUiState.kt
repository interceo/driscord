package com.driscord.presentation

import com.driscord.AppConfig
import com.driscord.domain.model.ConnectionState
import com.driscord.domain.model.PeerInfo
import com.driscord.domain.model.StreamStats

data class AppUiState(
    val connectionState: ConnectionState = ConnectionState.Disconnected,
    val localId: String = "",
    val peers: List<PeerInfo> = emptyList(),
    val muted: Boolean = false,
    val deafened: Boolean = false,
    val outputVolume: Float = 1f,
    val inputLevel: Float = 0f,
    val sharing: Boolean = false,
    val shareTargetName: String = "",
    val watching: Boolean = false,
    val streamingPeers: List<String> = emptyList(),
    val streamStats: StreamStats = StreamStats(),
    val config: AppConfig = AppConfig(),
    val systemAudioAvailable: Boolean = false,
    val showShareDialog: Boolean = false,
    val showSettings: Boolean = false,
)