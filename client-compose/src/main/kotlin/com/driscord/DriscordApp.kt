package com.driscord

import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import com.driscord.jni.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.json.Json
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo

// ---------------------------------------------------------------------------
// DriscordApp — orchestrates all native components
// ---------------------------------------------------------------------------

class DriscordApp(
    val config: AppConfig = AppConfig.loadDefault(),
) {
    // --- native component handles ---
    private val transportH: Long = NativeTransport.create()
    private val audioTransportH: Long = NativeAudioTransport.create(transportH)
    private val videoTransportH: Long = NativeVideoTransport.create(transportH)
    private val audioSenderH: Long = NativeAudioSender.create(audioTransportH)
    private val audioMixerH: Long = NativeAudioMixer.create()
    private val screenSessionH: Long =
        NativeScreenSession.create(
            config.screenBufferMs,
            config.maxSyncGapMs,
            videoTransportH,
            audioTransportH,
        )

    // Per-peer voice receivers: peerId -> NativeAudioReceiver handle
    private val voiceReceivers = HashMap<String, Long>()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val json = Json { ignoreUnknownKeys = true }

    // --- observable state ---
    private val _state = MutableStateFlow(AppState.Disconnected)
    val state: StateFlow<AppState> = _state.asStateFlow()

    private val _peers = MutableStateFlow<List<PeerInfo>>(emptyList())
    val peers: StateFlow<List<PeerInfo>> = _peers.asStateFlow()

    private val _streamingPeers = MutableStateFlow<List<String>>(emptyList())
    val streamingPeers: StateFlow<List<String>> = _streamingPeers.asStateFlow()

    private val _muted = MutableStateFlow(false)
    val muted: StateFlow<Boolean> = _muted.asStateFlow()

    private val _deafened = MutableStateFlow(false)
    val deafened: StateFlow<Boolean> = _deafened.asStateFlow()

    private val _volume = MutableStateFlow(1.0f)
    val volume: StateFlow<Float> = _volume.asStateFlow()

    private val _inputLevel = MutableStateFlow(0.0f)
    val inputLevel: StateFlow<Float> = _inputLevel.asStateFlow()

    private val _outputLevel = MutableStateFlow(0.0f)
    val outputLevel: StateFlow<Float> = _outputLevel.asStateFlow()

    private val _localId = MutableStateFlow("")
    val localId: StateFlow<String> = _localId.asStateFlow()

    private val _watching = MutableStateFlow(false)
    val watching: StateFlow<Boolean> = _watching.asStateFlow()
    private var watchingPeer: String = ""

    private val _sharing = MutableStateFlow(false)
    val sharing: StateFlow<Boolean> = _sharing.asStateFlow()

    private val _frames = MutableStateFlow<Map<String, ImageBitmap>>(emptyMap())
    val frames: StateFlow<Map<String, ImageBitmap>> = _frames.asStateFlow()

    private val _streamStats = MutableStateFlow(StreamStats())
    val streamStats: StateFlow<StreamStats> = _streamStats.asStateFlow()

    private val _config = MutableStateFlow(config)
    val currentConfig: StateFlow<AppConfig> = _config.asStateFlow()

    private val _shareTargetName = MutableStateFlow("")
    val shareTargetName: StateFlow<String> = _shareTargetName.asStateFlow()

    // ---------------------------------------------------------------------------
    // Init
    // ---------------------------------------------------------------------------

    init {
        // Apply TURN servers before any connection
        config.turnServers.forEach { ts ->
            NativeTransport.addTurnServer(transportH, ts.url, ts.user, ts.pass)
        }

        // Transport peer lifecycle
        NativeTransport.setOnPeerJoined(transportH) { peerId ->
            onPeerJoined(peerId)
        }
        NativeTransport.setOnPeerLeft(transportH) { peerId ->
            onPeerLeft(peerId)
        }

        // New streaming peer notification
        NativeVideoTransport.setOnNewStreamingPeer(videoTransportH) { peerId ->
            scope.launch(Dispatchers.Main) {
                if (peerId !in _streamingPeers.value) {
                    _streamingPeers.value = _streamingPeers.value + peerId
                }
            }
        }
        NativeVideoTransport.setOnStreamingPeerRemoved(videoTransportH) { peerId ->
            scope.launch(Dispatchers.Main) {
                _streamingPeers.value = _streamingPeers.value - peerId
                _frames.value = _frames.value - peerId
            }
        }

        // Screen session frame callbacks
        NativeScreenSession.setOnFrame(screenSessionH) { peerId, rgba, w, h ->
            scope.launch(Dispatchers.Main) {
                _frames.value = _frames.value + (peerId to rgbaToImageBitmap(rgba, w, h))
            }
        }
        NativeScreenSession.setOnFrameRemoved(screenSessionH) { peerId ->
            scope.launch(Dispatchers.Main) {
                _frames.value = _frames.value - peerId
            }
        }

        // Update loop
        scope.launch {
            while (isActive) {
                NativeScreenSession.update(screenSessionH)
                refreshVolatileState()
                delay(16)
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Actions
    // ---------------------------------------------------------------------------

    fun connect(serverUrl: String) {
        if (_state.value != AppState.Disconnected) return
        _state.value = AppState.Connecting
        NativeTransport.connect(transportH, serverUrl)
        scope.launch {
            while (isActive && !NativeTransport.connected(transportH)) delay(100)
            if (NativeTransport.connected(transportH)) {
                NativeAudioMixer.start(audioMixerH)
                NativeAudioSender.start(audioSenderH)
                withContext(Dispatchers.Main) {
                    _state.value = AppState.Connected
                    _localId.value = NativeTransport.localId(transportH)
                }
            }
        }
    }

    fun disconnect() {
        NativeTransport.disconnect(transportH)
        NativeAudioSender.stop(audioSenderH)
        NativeAudioMixer.stop(audioMixerH)
        synchronized(voiceReceivers) {
            voiceReceivers.values.forEach { recv ->
                NativeAudioMixer.removeSource(audioMixerH, recv)
                NativeAudioReceiver.destroy(recv)
            }
            voiceReceivers.clear()
        }
        _state.value = AppState.Disconnected
        _peers.value = emptyList()
        _streamingPeers.value = emptyList()
        _frames.value = emptyMap()
        _localId.value = ""
        _watching.value = false
        _sharing.value = false
    }

    fun toggleMute() {
        val next = !NativeAudioSender.muted(audioSenderH)
        NativeAudioSender.setMuted(audioSenderH, next)
        _muted.value = next
    }

    fun toggleDeafen() {
        val next = !NativeAudioMixer.deafened(audioMixerH)
        NativeAudioMixer.setDeafened(audioMixerH, next)
        _deafened.value = next
        // Muting mic when deafened matches typical voice-chat UX
        NativeAudioSender.setMuted(audioSenderH, next)
        _muted.value = NativeAudioSender.muted(audioSenderH)
    }

    fun setVolume(vol: Float) {
        NativeAudioMixer.setOutputVolume(audioMixerH, vol)
        _volume.value = vol
    }

    fun setPeerVolume(
        peerId: String,
        vol: Float,
    ) {
        val recv = synchronized(voiceReceivers) { voiceReceivers[peerId] } ?: return
        NativeAudioReceiver.setVolume(recv, vol)
    }

    fun peerVolume(peerId: String): Float {
        val recv = synchronized(voiceReceivers) { voiceReceivers[peerId] } ?: return 1f
        return NativeAudioReceiver.volume(recv)
    }

    fun joinStream(peerId: String) {
        watchingPeer = peerId
        NativeVideoTransport.setWatching(videoTransportH, true)
        NativeAudioTransport.setScreenAudioReceiver(audioTransportH, peerId, screenSessionH)
        NativeScreenSession.addAudioReceiverToMixer(screenSessionH, audioMixerH)
        _watching.value = true
    }

    fun leaveStream() {
        NativeVideoTransport.setWatching(videoTransportH, false)
        NativeScreenSession.removeAudioReceiverFromMixer(screenSessionH, audioMixerH)
        NativeAudioTransport.unsetScreenAudioReceiver(audioTransportH, watchingPeer)
        watchingPeer = ""
        NativeScreenSession.resetAudioReceiver(screenSessionH)
        _watching.value = false
    }

    fun setStreamVolume(vol: Float) = NativeScreenSession.setStreamVolume(screenSessionH, vol)

    fun streamVolume(): Float = NativeScreenSession.streamVolume(screenSessionH)

    fun saveConfig(newConfig: AppConfig) {
        val validated = newConfig.validated()
        _config.value = validated
        val path = AppConfig.defaultConfigPath()
        try {
            AppConfig.save(validated, path)
            println("[config] saved to $path")
        } catch (e: Exception) {
            println("[config] save failed: ${e.message}")
        }
    }

    fun startSharing(
        target: CaptureTarget,
        quality: Int,
        fps: Int,
        shareAudio: Boolean,
    ) {
        // quality is an index: 0=Source, 1=720p, 2=1080p, 3=1440p
        val (maxW, maxH) =
            when (quality) {
                0 -> 0 to 0
                1 -> 1280 to 720
                2 -> 1920 to 1080
                3 -> 2560 to 1440
                else -> 1920 to 1080
            }
        val targetJson = json.encodeToString(CaptureTarget.serializer(), target)
        val ok =
            NativeScreenSession.startSharing(
                screenSessionH,
                targetJson,
                maxW,
                maxH,
                fps,
                config.videoBitrateKbps,
                config.gopSize,
                shareAudio,
            )
        if (ok) {
            _sharing.value = true
            _shareTargetName.value = target.name
        }
    }

    fun stopSharing() {
        NativeScreenSession.stopSharing(screenSessionH)
        _sharing.value = false
        _shareTargetName.value = ""
    }

    val systemAudioAvailable: Boolean get() = NativeScreenCapture.systemAudioAvailable()

    fun listCaptureTargets(): List<CaptureTarget> = json.decodeFromString(NativeScreenCapture.listTargets())

    fun grabThumbnail(
        target: CaptureTarget,
        maxW: Int = 320,
        maxH: Int = 180,
    ): ImageBitmap? {
        val targetJson = json.encodeToString(CaptureTarget.serializer(), target)
        val rgba = NativeScreenCapture.grabThumbnail(targetJson, maxW, maxH) ?: return null
        val pixels = rgba.size / 4
        val w = maxW.coerceAtMost(pixels)
        val h = if (w > 0) pixels / w else 0
        if (w == 0 || h == 0) return null
        return rgbaToImageBitmap(rgba, w, h)
    }

    // ---------------------------------------------------------------------------
    // Peer lifecycle (called from C++ callbacks, may be on any thread)
    // ---------------------------------------------------------------------------

    private fun onPeerJoined(peerId: String) {
        val recv = NativeAudioReceiver.create(config.voiceJitterMs)
        synchronized(voiceReceivers) { voiceReceivers[peerId] = recv }
        NativeAudioTransport.registerVoiceReceiver(audioTransportH, peerId, recv)
        NativeAudioMixer.addSource(audioMixerH, recv)
        scope.launch(Dispatchers.Main) { refreshPeers() }
    }

    private fun onPeerLeft(peerId: String) {
        val recv = synchronized(voiceReceivers) { voiceReceivers.remove(peerId) }
        if (recv != null) {
            NativeAudioTransport.unregisterVoiceReceiver(audioTransportH, peerId)
            NativeAudioMixer.removeSource(audioMixerH, recv)
            NativeAudioReceiver.destroy(recv)
        }
        NativeVideoTransport.removeStreamingPeer(videoTransportH, peerId) // fires setOnStreamingPeerRemoved
        scope.launch(Dispatchers.Main) { refreshPeers() }
    }

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    private fun refreshPeers() {
        _peers.value = json.decodeFromString(NativeTransport.peers(transportH))
    }

    private suspend fun refreshVolatileState() =
        withContext(Dispatchers.Main) {
            if (_state.value == AppState.Connected) {
                _muted.value = NativeAudioSender.muted(audioSenderH)
                _deafened.value = NativeAudioMixer.deafened(audioMixerH)
                _inputLevel.value = NativeAudioSender.inputLevel(audioSenderH)
                _outputLevel.value = NativeAudioMixer.outputLevel(audioMixerH)
                _sharing.value = NativeScreenSession.sharing(screenSessionH)
                _watching.value = NativeVideoTransport.watching(videoTransportH)
                _streamStats.value = json.decodeFromString(NativeScreenSession.stats(screenSessionH))
                refreshPeers()
            }
        }

    // ---------------------------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------------------------

    fun close() {
        scope.cancel()
        disconnect()
        NativeScreenSession.destroy(screenSessionH)
        NativeAudioSender.destroy(audioSenderH)
        NativeAudioMixer.destroy(audioMixerH)
        NativeVideoTransport.destroy(videoTransportH)
        NativeAudioTransport.destroy(audioTransportH)
        NativeTransport.destroy(transportH)
    }

    // ---------------------------------------------------------------------------
    // Companion
    // ---------------------------------------------------------------------------

    companion object {
        fun rgbaToImageBitmap(
            rgba: ByteArray,
            w: Int,
            h: Int,
        ): ImageBitmap {
            val bitmap = Bitmap()
            bitmap.allocPixels(ImageInfo(w, h, ColorType.RGBA_8888, ColorAlphaType.UNPREMUL))
            bitmap.installPixels(rgba)
            return bitmap.asComposeImageBitmap()
        }
    }
}
