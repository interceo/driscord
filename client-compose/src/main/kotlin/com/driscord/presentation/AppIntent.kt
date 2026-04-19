package com.driscord.presentation

import com.driscord.AppConfig
import com.driscord.domain.model.CaptureTarget

sealed interface AppIntent {
    // Voice connection (signaling server)
    data class Connect(val serverUrl: String) : AppIntent
    object Disconnect : AppIntent

    // Audio controls
    object ToggleMute : AppIntent
    object ToggleDeafen : AppIntent
    data class SetOutputVolume(val volume: Float) : AppIntent
    data class SetPeerVolume(val peerId: String, val volume: Float) : AppIntent

    // Screen share
    object OpenShareDialog : AppIntent
    object DismissShareDialog : AppIntent
    data class StartSharing(val target: CaptureTarget, val quality: Int, val fps: Int, val shareAudio: Boolean) : AppIntent
    object StopSharing : AppIntent

    // Stream watching
    object JoinStream : AppIntent
    object LeaveStream : AppIntent
    data class SetStreamVolume(val peerId: String, val volume: Float) : AppIntent

    // Settings
    object OpenSettings : AppIntent
    object DismissSettings : AppIntent
    data class SaveConfig(val config: AppConfig) : AppIntent

    // Auth
    data class Login(val username: String, val password: String) : AppIntent
    data class Register(val username: String, val email: String, val password: String) : AppIntent
    object Logout : AppIntent
    object DismissApiError : AppIntent

    // Servers & channels
    data class SelectServer(val serverId: Int) : AppIntent
    data class SelectChannel(val channelId: Int) : AppIntent
    object OpenCreateServerDialog : AppIntent
    object DismissCreateServerDialog : AppIntent
    data class CreateServer(val name: String) : AppIntent
    data class OpenCreateChannelDialog(val defaultKind: com.driscord.domain.model.ChannelKind = com.driscord.domain.model.ChannelKind.voice) : AppIntent
    object DismissCreateChannelDialog : AppIntent
    data class CreateChannel(val name: String, val kind: com.driscord.domain.model.ChannelKind) : AppIntent

    // Invites
    data class CreateInvite(val serverId: Int) : AppIntent
    object OpenJoinByInviteDialog : AppIntent
    object DismissInviteDialog : AppIntent
    data class AcceptInvite(val code: String) : AppIntent
}
