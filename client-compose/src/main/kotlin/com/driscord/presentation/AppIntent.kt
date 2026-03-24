package com.driscord.presentation

import com.driscord.AppConfig
import com.driscord.domain.model.CaptureTarget

sealed interface AppIntent {
    data class Connect(val serverUrl: String) : AppIntent
    object Disconnect : AppIntent

    object ToggleMute : AppIntent
    object ToggleDeafen : AppIntent
    data class SetOutputVolume(val volume: Float) : AppIntent
    data class SetPeerVolume(val peerId: String, val volume: Float) : AppIntent

    object OpenShareDialog : AppIntent
    object DismissShareDialog : AppIntent
    data class StartSharing(val target: CaptureTarget, val quality: Int, val fps: Int, val shareAudio: Boolean) : AppIntent
    object StopSharing : AppIntent

    object JoinStream : AppIntent
    object LeaveStream : AppIntent
    data class SetStreamVolume(val peerId: String, val volume: Float) : AppIntent

    object OpenSettings : AppIntent
    object DismissSettings : AppIntent
    data class SaveConfig(val config: AppConfig) : AppIntent
}