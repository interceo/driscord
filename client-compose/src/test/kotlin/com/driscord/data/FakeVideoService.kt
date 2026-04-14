package com.driscord.data

import androidx.compose.ui.graphics.ImageBitmap
import com.driscord.data.video.VideoService
import com.driscord.domain.model.CaptureTarget
import com.driscord.domain.model.StreamStats
import kotlinx.coroutines.flow.MutableStateFlow

class FakeVideoService : VideoService {

    override val sharing = MutableStateFlow(false)
    override val shareTargetName = MutableStateFlow("")
    override val watching = MutableStateFlow(false)
    override val streamingPeers = MutableStateFlow<List<String>>(emptyList())
    override val frames = MutableStateFlow<Map<String, ImageBitmap>>(emptyMap())
    override val streamStats = MutableStateFlow(StreamStats())
    override val systemAudioAvailable: Boolean = false

    var startSharingResult = true
    var startSharingCalls = mutableListOf<CaptureTarget>()
    var stopSharingCalled = 0
    var joinStreamCalled = 0
    var leaveStreamCalled = 0
    var streamVolumes = mutableMapOf<String, Float>()
    var destroyCalled = 0

    override fun joinStream() { joinStreamCalled++ }
    override fun leaveStream() { leaveStreamCalled++ }

    override fun startSharing(target: CaptureTarget, quality: Int, fps: Int, shareAudio: Boolean): Boolean {
        startSharingCalls += target
        return startSharingResult
    }

    override fun stopSharing() { stopSharingCalled++ }

    override fun setStreamVolume(peerId: String, vol: Float) { streamVolumes[peerId] = vol }
    override fun getStreamVolume(): Float = 1.0f

    override fun listCaptureTargets(): List<CaptureTarget> = emptyList()
    override fun grabThumbnail(target: CaptureTarget): ImageBitmap? = null

    override fun destroy() { destroyCalled++ }
}
