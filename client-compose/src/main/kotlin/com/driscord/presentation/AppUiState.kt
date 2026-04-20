package com.driscord.presentation

import com.driscord.AppConfig
import com.driscord.domain.model.Channel
import com.driscord.domain.model.ChannelKind
import com.driscord.domain.model.ConnectionState
import com.driscord.domain.model.PeerInfo
import com.driscord.domain.model.Server
import com.driscord.domain.model.StreamStats
import com.driscord.domain.model.UserProfile

enum class AuthStatus { LoggedOut, LoggingIn, Restoring, LoggedIn }
enum class SettingsPage { MyAccount, Audio, Advanced }

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
    // Auth
    val authStatus: AuthStatus = AuthStatus.LoggedOut,
    val currentUsername: String = "",
    val currentUserProfile: UserProfile? = null,
    val profileSaving: Boolean = false,
    val profileError: String? = null,
    // Servers & channels
    val servers: List<Server> = emptyList(),
    val selectedServerId: Int? = null,
    val channels: List<Channel> = emptyList(),
    val selectedChannelId: Int? = null,
    // Error feedback from API
    val apiError: String? = null,
    // Dialogs
    val showCreateServerDialog: Boolean = false,
    val showCreateChannelDialog: Boolean = false,
    val createChannelDefaultKind: ChannelKind = ChannelKind.voice,
    val showJoinByInviteDialog: Boolean = false,
    val inviteDialogCode: String? = null,
    val inviteDialogServerName: String = "",
)
