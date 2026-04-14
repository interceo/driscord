package com.driscord.presentation.viewmodel

import com.driscord.data.audio.AudioInputDevice
import com.driscord.data.audio.AudioService
import com.driscord.data.config.ConfigRepository
import com.driscord.data.connection.ConnectionService
import com.driscord.data.video.VideoService
import com.driscord.domain.model.CaptureTarget
import androidx.compose.ui.graphics.ImageBitmap
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class AppViewModel(
    private val connectionService: ConnectionService,
    private val audioService: AudioService,
    private val videoService: VideoService,
    private val configRepository: ConfigRepository,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
) {

    private val _state = MutableStateFlow(AppUiState(
        systemAudioAvailable = videoService.systemAudioAvailable,
        config = configRepository.config.value,
    ))
    val state: StateFlow<AppUiState> = _state.asStateFlow()

    init {
        // Merge service flows into single AppUiState
        scope.launch { connectionService.connectionState.collect { v -> _state.update { it.copy(connectionState = v) } } }
        scope.launch { connectionService.localId.collect { v -> _state.update { it.copy(localId = v) } } }
        scope.launch { connectionService.peers.collect { v -> _state.update { it.copy(peers = v) } } }
        scope.launch { audioService.muted.collect { v -> _state.update { it.copy(muted = v) } } }
        scope.launch { audioService.deafened.collect { v -> _state.update { it.copy(deafened = v) } } }
        scope.launch { audioService.outputVolume.collect { v -> _state.update { it.copy(outputVolume = v) } } }
        scope.launch { audioService.inputLevel.collect { v -> _state.update { it.copy(inputLevel = v) } } }
        scope.launch { videoService.sharing.collect { v -> _state.update { it.copy(sharing = v) } } }
        scope.launch { videoService.shareTargetName.collect { v -> _state.update { it.copy(shareTargetName = v) } } }
        scope.launch { videoService.watching.collect { v -> _state.update { it.copy(watching = v) } } }
        scope.launch { videoService.streamingPeers.collect { v -> _state.update { it.copy(streamingPeers = v) } } }
        scope.launch { videoService.frames.collect { v -> _state.update { it.copy(frames = v) } } }
        scope.launch { videoService.streamStats.collect { v -> _state.update { it.copy(streamStats = v) } } }
        scope.launch { configRepository.config.collect { v -> _state.update { it.copy(config = v) } } }

        // Peer lifecycle → AudioService
        scope.launch {
            connectionService.peerJoinedEvents.collect { peerId ->
                audioService.onPeerJoined(peerId)
            }
        }
        scope.launch {
            connectionService.peerLeftEvents.collect { peerId ->
                audioService.onPeerLeft(peerId)
            }
        }
    }

    fun onIntent(intent: AppIntent) {
        when (intent) {
            is AppIntent.Connect -> {
                val cfg = configRepository.config.value
                connectionService.connect(intent.serverUrl)
                audioService.setInputDevice(cfg.micDeviceId)
                audioService.setOutputDevice(cfg.outputDeviceId)
                audioService.start()
                audioService.setNoiseGate(cfg.noiseGateThreshold)
            }
            AppIntent.Disconnect -> {
                connectionService.disconnect()
                audioService.stop()
                videoService.stopSharing()
            }
            AppIntent.ToggleMute -> audioService.toggleMute()
            AppIntent.ToggleDeafen -> audioService.toggleDeafen()
            is AppIntent.SetOutputVolume -> audioService.setOutputVolume(intent.volume)
            is AppIntent.SetPeerVolume -> audioService.setPeerVolume(intent.peerId, intent.volume)

            AppIntent.OpenShareDialog -> _state.update { it.copy(showShareDialog = true) }
            AppIntent.DismissShareDialog -> _state.update { it.copy(showShareDialog = false) }
            is AppIntent.StartSharing -> {
                val ok = videoService.startSharing(
                    intent.target, intent.quality, intent.fps, intent.shareAudio,
                )
                if (ok) _state.update { it.copy(showShareDialog = false) }
            }
            AppIntent.StopSharing -> videoService.stopSharing()

            AppIntent.JoinStream -> videoService.joinStream()
            AppIntent.LeaveStream -> videoService.leaveStream()
            is AppIntent.SetStreamVolume -> videoService.setStreamVolume(intent.peerId, intent.volume)

            AppIntent.OpenSettings -> _state.update { it.copy(showSettings = true) }
            AppIntent.DismissSettings -> _state.update { it.copy(showSettings = false) }
            is AppIntent.SaveConfig -> {
                val prev = configRepository.config.value
                configRepository.save(intent.config)
                if (intent.config.micDeviceId != prev.micDeviceId) {
                    audioService.setInputDevice(intent.config.micDeviceId)
                }
                if (intent.config.outputDeviceId != prev.outputDeviceId) {
                    audioService.setOutputDevice(intent.config.outputDeviceId)
                }
                _state.update { it.copy(showSettings = false) }
            }
        }
    }

    // Synchronous getters — вызываются только при открытии меню/диалога, не нужны в state
    fun getPeerVolume(peerId: String): Float = audioService.getPeerVolume(peerId)
    fun getStreamVolume(): Float = videoService.getStreamVolume()
    fun listInputDevices(): List<AudioInputDevice> = audioService.listInputDevices()
    fun listOutputDevices(): List<AudioInputDevice> = audioService.listOutputDevices()
    fun listCaptureTargets(): List<CaptureTarget> = videoService.listCaptureTargets()
    fun grabThumbnail(target: CaptureTarget): ImageBitmap? = videoService.grabThumbnail(target)

    fun close() {
        scope.cancel()
        videoService.destroy()
        audioService.destroy()
        connectionService.destroy()
    }
}