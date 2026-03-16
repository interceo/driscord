package com.driscord

import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo

// ---------------------------------------------------------------------------
// Data models
// ---------------------------------------------------------------------------

enum class AppState { Disconnected, Connecting, Connected }

@Serializable
data class PeerInfo(val id: String, val connected: Boolean)

@Serializable
data class CaptureTarget(
    val type: Int,   // 0 = Monitor, 1 = Window
    val id: String,
    val name: String,
    val width: Int,
    val height: Int,
    val x: Int,
    val y: Int,
)

@Serializable
data class StreamStats(
    val width: Int = 0,
    val height: Int = 0,
    val measuredKbps: Int = 0,
    val video: JitterStats = JitterStats(),
    val audio: JitterStats = JitterStats(),
) {
    @Serializable
    data class JitterStats(
        val queue: Int = 0,
        val bufMs: Int = 0,
        val drops: Long = 0,
        val misses: Long = 0,
    )
}

// ---------------------------------------------------------------------------
// DriscordApp — the new App, written in Kotlin
// ---------------------------------------------------------------------------

class DriscordApp(
    host: String = "localhost",
    port: Int = 8080,
    screenFps: Int = 60,
    bitrateKbps: Int = 8000,
    voiceJitterMs: Int = 80,
    screenBufMs: Int = 120,
    maxSyncGapMs: Int = 2000,
) {
    private val handle: Long = NativeSession.create(
        host, port, screenFps, bitrateKbps, voiceJitterMs, screenBufMs, maxSyncGapMs
    )

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

    private val _sharing = MutableStateFlow(false)
    val sharing: StateFlow<Boolean> = _sharing.asStateFlow()

    /** RGBA bitmaps keyed by peer id */
    private val _frames = MutableStateFlow<Map<String, ImageBitmap>>(emptyMap())
    val frames: StateFlow<Map<String, ImageBitmap>> = _frames.asStateFlow()

    private val _streamStats = MutableStateFlow(StreamStats())
    val streamStats: StateFlow<StreamStats> = _streamStats.asStateFlow()

    // ---------------------------------------------------------------------------
    // Init
    // ---------------------------------------------------------------------------

    init {
        NativeSession.setOnFrame(handle) { peerId, rgba, w, h ->
            scope.launch(Dispatchers.Main) {
                val bitmap = rgbaToImageBitmap(rgba, w, h)
                _frames.value = _frames.value + (peerId to bitmap)
            }
        }
        NativeSession.setOnFrameRemoved(handle) { peerId ->
            scope.launch(Dispatchers.Main) {
                _frames.value = _frames.value - peerId
            }
        }
        NativeSession.setOnPeerJoined(handle) { _ ->
            scope.launch(Dispatchers.Main) { refreshPeers() }
        }
        NativeSession.setOnPeerLeft(handle) { _ ->
            scope.launch(Dispatchers.Main) { refreshPeers() }
        }

        // Update loop: polls C++ session and refreshes Kotlin state
        scope.launch {
            while (isActive) {
                NativeSession.update(handle)
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
        NativeSession.connect(handle, serverUrl)
        scope.launch {
            // Poll until connected
            while (isActive && !NativeSession.connected(handle)) delay(100)
            if (NativeSession.connected(handle)) {
                NativeSession.startAudio(handle)
                withContext(Dispatchers.Main) {
                    _state.value = AppState.Connected
                    _localId.value = NativeSession.localId(handle)
                }
            }
        }
    }

    fun disconnect() {
        NativeSession.disconnect(handle)
        _state.value = AppState.Disconnected
        _peers.value = emptyList()
        _streamingPeers.value = emptyList()
        _frames.value = emptyMap()
        _localId.value = ""
        _watching.value = false
        _sharing.value = false
    }

    fun toggleMute() {
        NativeSession.toggleMute(handle)
        _muted.value = NativeSession.muted(handle)
    }

    fun toggleDeafen() {
        NativeSession.toggleDeafen(handle)
        _deafened.value = NativeSession.deafened(handle)
        _muted.value = NativeSession.muted(handle)
    }

    fun setVolume(vol: Float) {
        NativeSession.setVolume(handle, vol)
        _volume.value = vol
    }

    fun setPeerVolume(peerId: String, vol: Float) = NativeSession.setPeerVolume(handle, peerId, vol)
    fun peerVolume(peerId: String): Float = NativeSession.peerVolume(handle, peerId)

    fun joinStream() {
        NativeSession.joinStream(handle)
        _watching.value = true
    }

    fun leaveStream() {
        NativeSession.leaveStream(handle)
        _watching.value = false
    }

    fun setStreamVolume(vol: Float) = NativeSession.setStreamVolume(handle, vol)
    fun streamVolume(): Float = NativeSession.streamVolume(handle)

    fun startSharing(target: CaptureTarget, quality: Int, fps: Int, shareAudio: Boolean) {
        val targetJson = json.encodeToString(CaptureTarget.serializer(), target)
        NativeSession.startSharing(handle, targetJson, quality, fps, shareAudio)
        _sharing.value = NativeSession.sharing(handle)
    }

    fun stopSharing() {
        NativeSession.stopSharing(handle)
        _sharing.value = false
    }

    val systemAudioAvailable: Boolean get() = NativeSession.systemAudioAvailable()

    fun listCaptureTargets(): List<CaptureTarget> {
        val raw = NativeSession.listCaptureTargets()
        return json.decodeFromString(raw)
    }

    fun grabThumbnail(target: CaptureTarget, maxW: Int = 320, maxH: Int = 180): ImageBitmap? {
        val targetJson = json.encodeToString(CaptureTarget.serializer(), target)
        val rgba = NativeSession.grabThumbnail(targetJson, maxW, maxH) ?: return null
        // Width/height come back embedded in the capture; we use maxW/maxH as a hint.
        // The actual decoded size may differ — compute from byte count.
        val pixels = rgba.size / 4
        val w = maxW.coerceAtMost(pixels)
        val h = if (w > 0) pixels / w else 0
        if (w == 0 || h == 0) return null
        return rgbaToImageBitmap(rgba, w, h)
    }

    // ---------------------------------------------------------------------------
    // Internal refresh helpers
    // ---------------------------------------------------------------------------

    private fun refreshPeers() {
        _peers.value = json.decodeFromString(_peers.let { NativeSession.peers(handle) })
        _streamingPeers.value = json.decodeFromString(NativeSession.streamingPeers(handle))
    }

    private suspend fun refreshVolatileState() = withContext(Dispatchers.Main) {
        if (_state.value == AppState.Connected) {
            _muted.value = NativeSession.muted(handle)
            _deafened.value = NativeSession.deafened(handle)
            _inputLevel.value = NativeSession.inputLevel(handle)
            _outputLevel.value = NativeSession.outputLevel(handle)
            _sharing.value = NativeSession.sharing(handle)
            _watching.value = NativeSession.watchingStream(handle)
            _streamStats.value = json.decodeFromString(NativeSession.streamStats(handle))
            // Refresh peer list periodically
            refreshPeers()
        }
    }

    // ---------------------------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------------------------

    fun close() {
        scope.cancel()
        NativeSession.disconnect(handle)
        NativeSession.destroy(handle)
    }

    // ---------------------------------------------------------------------------
    // Companion helpers
    // ---------------------------------------------------------------------------

    companion object {
        private fun rgbaToImageBitmap(rgba: ByteArray, w: Int, h: Int): ImageBitmap {
            val bitmap = Bitmap()
            bitmap.allocPixels(ImageInfo(w, h, ColorType.RGBA_8888, ColorAlphaType.UNPREMUL))
            bitmap.installPixels(rgba)
            return bitmap.asComposeImageBitmap()
        }
    }
}
