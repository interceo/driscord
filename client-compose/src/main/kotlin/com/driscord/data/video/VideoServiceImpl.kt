package com.driscord.data.video

import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import com.driscord.AppConfig
import com.driscord.domain.model.CaptureTarget
import com.driscord.domain.model.StreamStats
import com.driscord.jni.NativeDriscord
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo

class VideoServiceImpl(
    config: AppConfig,
) : VideoService {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val json = Json { ignoreUnknownKeys = true }

    private val _sharing = MutableStateFlow(false)
    override val sharing: StateFlow<Boolean> = _sharing.asStateFlow()

    private val _shareTargetName = MutableStateFlow("")
    override val shareTargetName: StateFlow<String> = _shareTargetName.asStateFlow()

    private val _watching = MutableStateFlow<Boolean>(false)
    override val watching: StateFlow<Boolean> = _watching.asStateFlow()

    private val _streamingPeers = MutableStateFlow<List<String>>(emptyList())
    override val streamingPeers: StateFlow<List<String>> = _streamingPeers.asStateFlow()

    private val _frames = MutableStateFlow<Map<String, ImageBitmap>>(emptyMap())
    override val frames: StateFlow<Map<String, ImageBitmap>> = _frames.asStateFlow()

    private val _streamStats = MutableStateFlow(StreamStats())
    override val streamStats: StateFlow<StreamStats> = _streamStats.asStateFlow()

    override val systemAudioAvailable: Boolean
        get() = NativeDriscord.captureSystemAudioAvailable()

    init {
        NativeDriscord.screenInit()

        NativeDriscord.setOnNewStreamingPeer { peerId ->
            scope.launch(Dispatchers.Main) {
                if (peerId !in _streamingPeers.value)
                    _streamingPeers.value += peerId
                // Auto-join new peers while already watching.
                if (_watching.value) {
                    NativeDriscord.screenJoinStream(peerId)
                }
            }
        }
        NativeDriscord.setOnStreamingPeerRemoved { peerId ->
            scope.launch(Dispatchers.Main) {
                _streamingPeers.value -= peerId
                _frames.value -= peerId
            }
        }
        // Signaling-based streaming notifications (immediate, no video data dependency).
        NativeDriscord.setOnStreamingStarted { peerId ->
            scope.launch(Dispatchers.Main) {
                if (peerId !in _streamingPeers.value)
                    _streamingPeers.value += peerId
                if (_watching.value) {
                    NativeDriscord.screenJoinStream(peerId)
                }
            }
        }
        NativeDriscord.setOnStreamingStopped { peerId ->
            scope.launch(Dispatchers.Main) {
                _streamingPeers.value -= peerId
                _frames.value -= peerId
            }
        }
        NativeDriscord.setOnFrame { peerId, rgba, w, h ->
            scope.launch(Dispatchers.Main) {
                _frames.value += (peerId to rgbaToImageBitmap(rgba, w, h))
            }
        }
        NativeDriscord.setOnFrameRemoved { peerId ->
            scope.launch(Dispatchers.Main) { _frames.value -= peerId }
        }

        // Update loop: screen session tick + stats
        scope.launch {
            while (isActive) {
                NativeDriscord.screenUpdate()
                withContext(Dispatchers.Main) {
                    _sharing.value = NativeDriscord.screenSharing()
                    _watching.value = NativeDriscord.videoWatching()
                    _streamStats.value = json.decodeFromString(NativeDriscord.screenStats())
                }
                delay(16)
            }
        }
    }

    override fun joinStream() {
        _streamingPeers.value.forEach { NativeDriscord.screenJoinStream(it) }
        _watching.value = true
    }

    override fun leaveStream() {
        NativeDriscord.screenLeaveStream()
        _watching.value = false
    }

    override fun startSharing(
        target: CaptureTarget,
        quality: Int,
        fps: Int,
        shareAudio: Boolean,
    ): Boolean {
        val (maxW, maxH) = when (quality) {
            0 -> 0 to 0
            1 -> 1280 to 720
            2 -> 1920 to 1080
            3 -> 2560 to 1440
            else -> 1920 to 1080
        }
        val targetJson = json.encodeToString(CaptureTarget.serializer(), target)
        val err = NativeDriscord.screenStartSharing(
            targetJson, maxW, maxH, fps, shareAudio,
        )
        if (err != null) {
            System.err.println("VideoService: screenStartSharing failed: $err")
            return false
        }
        _sharing.value = true
        _shareTargetName.value = target.name
        return true
    }

    override fun stopSharing() {
        NativeDriscord.screenStopSharing()
        _sharing.value = false
        _shareTargetName.value = ""
    }

    override fun setStreamVolume(peerId: String, vol: Float) = NativeDriscord.screenSetStreamVolume(peerId, vol)

    override fun getStreamVolume(): Float = NativeDriscord.screenStreamVolume()

    override fun listCaptureTargets(): List<CaptureTarget> =
        json.decodeFromString(NativeDriscord.captureVideoListTargets())

    override fun grabThumbnail(target: CaptureTarget): ImageBitmap? {
        val maxW = 320; val maxH = 180
        val targetJson = json.encodeToString(CaptureTarget.serializer(), target)
        val rgba = NativeDriscord.captureGrabThumbnail(targetJson, maxW, maxH) ?: return null
        val pixels = rgba.size / 4
        val w = maxW.coerceAtMost(pixels)
        val h = if (w > 0) pixels / w else 0
        if (w == 0 || h == 0) return null
        return rgbaToImageBitmap(rgba, w, h)
    }

    override fun destroy() {
        scope.cancel()
        NativeDriscord.screenDeinit()
    }

    companion object {
        fun rgbaToImageBitmap(rgba: ByteArray, w: Int, h: Int): ImageBitmap {
            val bitmap = Bitmap()
            bitmap.allocPixels(ImageInfo(w, h, ColorType.RGBA_8888, ColorAlphaType.UNPREMUL))
            bitmap.installPixels(rgba)
            return bitmap.asComposeImageBitmap()
        }
    }
}
