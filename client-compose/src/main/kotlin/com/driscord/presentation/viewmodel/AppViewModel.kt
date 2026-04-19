package com.driscord.presentation.viewmodel

import com.driscord.data.api.AuthRepository
import com.driscord.data.api.ServerRepository
import com.driscord.data.audio.AudioDevice
import com.driscord.data.audio.AudioService
import com.driscord.data.config.ConfigRepository
import com.driscord.data.connection.ConnectionService
import com.driscord.data.video.VideoService
import com.driscord.domain.model.CaptureTarget
import com.driscord.domain.model.ChannelKind
import androidx.compose.ui.graphics.ImageBitmap
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import com.driscord.presentation.AuthStatus
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class AppViewModel(
    private val connectionService: ConnectionService,
    private val audioService: AudioService,
    private val videoService: VideoService,
    private val configRepository: ConfigRepository,
    private val authRepository: AuthRepository,
    private val serverRepository: ServerRepository,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
) {

    private val _state = MutableStateFlow(AppUiState(
        systemAudioAvailable = videoService.systemAudioAvailable,
        config = configRepository.config.value,
    ))
    val state: StateFlow<AppUiState> = _state.asStateFlow()

    /** Frames are kept in a separate flow to avoid copying the full AppUiState on every frame. */
    val frames: StateFlow<Map<String, ImageBitmap>> = videoService.frames

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

        // Restore persisted session: refresh token on every startup
        if (authRepository.isLoggedIn) {
            _state.update { it.copy(authStatus = AuthStatus.Restoring) }
            scope.launch {
                authRepository.refreshSession()
                    .onSuccess {
                        val username = authRepository.currentUsername ?: ""
                        connectionService.setLocalUsername(username)
                        _state.update { it.copy(authStatus = AuthStatus.LoggedIn, currentUsername = username) }
                        refreshServers()
                    }
                    .onFailure {
                        authRepository.logout()
                        _state.update { it.copy(authStatus = AuthStatus.LoggedOut) }
                    }
            }
        }
    }

    fun onIntent(intent: AppIntent) {
        when (intent) {
            // ------------------------------------------------------------------
            // Voice connection
            // ------------------------------------------------------------------
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
                _state.update { it.copy(selectedChannelId = null) }
            }

            // ------------------------------------------------------------------
            // Audio
            // ------------------------------------------------------------------
            AppIntent.ToggleMute -> audioService.toggleMute()
            AppIntent.ToggleDeafen -> audioService.toggleDeafen()
            is AppIntent.SetOutputVolume -> audioService.setOutputVolume(intent.volume)
            is AppIntent.SetPeerVolume -> audioService.setPeerVolume(intent.peerId, intent.volume)

            // ------------------------------------------------------------------
            // Screen share
            // ------------------------------------------------------------------
            AppIntent.OpenShareDialog -> _state.update { it.copy(showShareDialog = true) }
            AppIntent.DismissShareDialog -> _state.update { it.copy(showShareDialog = false) }
            is AppIntent.StartSharing -> {
                val ok = videoService.startSharing(
                    intent.target, intent.quality, intent.fps, intent.shareAudio,
                )
                if (ok) _state.update { it.copy(showShareDialog = false) }
            }
            AppIntent.StopSharing -> videoService.stopSharing()

            // ------------------------------------------------------------------
            // Stream watching
            // ------------------------------------------------------------------
            AppIntent.JoinStream -> videoService.joinStream()
            AppIntent.LeaveStream -> videoService.leaveStream()
            is AppIntent.SetStreamVolume -> videoService.setStreamVolume(intent.peerId, intent.volume)

            // ------------------------------------------------------------------
            // Settings
            // ------------------------------------------------------------------
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

            // ------------------------------------------------------------------
            // Auth
            // ------------------------------------------------------------------
            is AppIntent.Login -> scope.launch {
                _state.update { it.copy(authStatus = AuthStatus.LoggingIn, apiError = null) }
                authRepository.login(intent.username, intent.password)
                    .onSuccess {
                        val username = authRepository.currentUsername ?: ""
                        connectionService.setLocalUsername(username)
                        _state.update { it.copy(authStatus = AuthStatus.LoggedIn, currentUsername = username) }
                        refreshServers()
                    }
                    .onFailure { e ->
                        _state.update { it.copy(authStatus = AuthStatus.LoggedOut, apiError = e.message) }
                    }
            }
            is AppIntent.Register -> scope.launch {
                _state.update { it.copy(authStatus = AuthStatus.LoggingIn, apiError = null) }
                authRepository.register(intent.username, intent.email, intent.password)
                    .onSuccess {
                        val username = authRepository.currentUsername ?: ""
                        connectionService.setLocalUsername(username)
                        _state.update { it.copy(authStatus = AuthStatus.LoggedIn, currentUsername = username) }
                        refreshServers()
                    }
                    .onFailure { e ->
                        _state.update { it.copy(authStatus = AuthStatus.LoggedOut, apiError = e.message) }
                    }
            }
            AppIntent.Logout -> {
                authRepository.logout()
                connectionService.disconnect()
                audioService.stop()
                videoService.stopSharing()
                _state.update {
                    AppUiState(
                        systemAudioAvailable = videoService.systemAudioAvailable,
                        config = configRepository.config.value,
                    )
                }
            }
            AppIntent.DismissApiError -> _state.update { it.copy(apiError = null) }

            // ------------------------------------------------------------------
            // Servers & channels
            // ------------------------------------------------------------------
            is AppIntent.SelectServer -> scope.launch {
                _state.update { it.copy(selectedServerId = intent.serverId, channels = emptyList(), selectedChannelId = null) }
                serverRepository.listChannels(intent.serverId)
                    .onSuccess { channels -> _state.update { it.copy(channels = channels) } }
                    .onFailure { e -> _state.update { it.copy(apiError = e.message) } }
            }
            is AppIntent.SelectChannel -> {
                val channel = _state.value.channels.find { it.id == intent.channelId } ?: return
                if (channel.kind != ChannelKind.voice) return
                _state.update { it.copy(selectedChannelId = intent.channelId) }
                // Connect to the signaling server; channel id passed as path segment
                val url = configRepository.config.value.serverUrl + "/channels/${intent.channelId}"
                onIntent(AppIntent.Connect(url))
            }

            AppIntent.OpenCreateServerDialog -> _state.update { it.copy(showCreateServerDialog = true) }
            AppIntent.DismissCreateServerDialog -> _state.update { it.copy(showCreateServerDialog = false) }
            is AppIntent.CreateServer -> scope.launch {
                serverRepository.createServer(intent.name)
                    .onSuccess { server ->
                        _state.update { s ->
                            s.copy(
                                servers = s.servers + server,
                                showCreateServerDialog = false,
                            )
                        }
                    }
                    .onFailure { e -> _state.update { it.copy(apiError = e.message) } }
            }

            is AppIntent.OpenCreateChannelDialog -> _state.update { it.copy(showCreateChannelDialog = true, createChannelDefaultKind = intent.defaultKind) }
            AppIntent.DismissCreateChannelDialog -> _state.update { it.copy(showCreateChannelDialog = false) }
            is AppIntent.CreateChannel -> scope.launch {
                val serverId = _state.value.selectedServerId ?: return@launch
                serverRepository.createChannel(serverId, intent.name, intent.kind)
                    .onSuccess { channel ->
                        _state.update { s ->
                            s.copy(
                                channels = (s.channels + channel).sortedWith(
                                    compareBy({ it.kind.ordinal }, { it.position }, { it.id })
                                ),
                                showCreateChannelDialog = false,
                            )
                        }
                    }
                    .onFailure { e -> _state.update { it.copy(apiError = e.message) } }
            }

            // ------------------------------------------------------------------
            // Invites
            // ------------------------------------------------------------------
            is AppIntent.CreateInvite -> scope.launch {
                val serverName = _state.value.servers.find { it.id == intent.serverId }?.name ?: ""
                serverRepository.createInvite(intent.serverId)
                    .onSuccess { code ->
                        _state.update {
                            it.copy(
                                inviteDialogCode = code,
                                inviteDialogServerName = serverName,
                                showJoinByInviteDialog = false,
                            )
                        }
                    }
                    .onFailure { e -> _state.update { it.copy(apiError = e.message) } }
            }
            AppIntent.OpenJoinByInviteDialog -> _state.update {
                it.copy(showJoinByInviteDialog = true, inviteDialogCode = null)
            }
            AppIntent.DismissInviteDialog -> _state.update {
                it.copy(
                    showJoinByInviteDialog = false,
                    inviteDialogCode = null,
                    inviteDialogServerName = "",
                )
            }
            is AppIntent.AcceptInvite -> scope.launch {
                serverRepository.acceptInvite(intent.code)
                    .onSuccess { serverId ->
                        _state.update {
                            it.copy(
                                showJoinByInviteDialog = false,
                                inviteDialogCode = null,
                                inviteDialogServerName = "",
                            )
                        }
                        refreshServers()
                        onIntent(AppIntent.SelectServer(serverId))
                    }
                    .onFailure { e -> _state.update { it.copy(apiError = e.message) } }
            }
        }
    }

    // ------------------------------------------------------------------
    // Synchronous getters — called only when opening menus/dialogs
    // ------------------------------------------------------------------
    fun getPeerVolume(peerId: String): Float = audioService.getPeerVolume(peerId)
    fun getStreamVolume(): Float = videoService.getStreamVolume()
    fun listInputDevices(): List<AudioDevice> = audioService.listInputDevices()
    fun listOutputDevices(): List<AudioDevice> = audioService.listOutputDevices()
    fun listCaptureTargets(): List<CaptureTarget> = videoService.listCaptureTargets()
    fun grabThumbnail(target: CaptureTarget): ImageBitmap? = videoService.grabThumbnail(target)

    fun close() {
        scope.cancel()
        videoService.destroy()
        audioService.destroy()
        connectionService.destroy()
    }

    // ------------------------------------------------------------------
    // Private helpers
    // ------------------------------------------------------------------
    private suspend fun refreshServers() {
        serverRepository.listServers()
            .onSuccess { servers -> _state.update { it.copy(servers = servers) } }
            .onFailure { e -> _state.update { it.copy(apiError = e.message) } }
    }
}
