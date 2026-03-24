package com.driscord.data.video

import androidx.compose.ui.graphics.ImageBitmap
import com.driscord.domain.model.CaptureTarget
import com.driscord.domain.model.StreamStats
import kotlinx.coroutines.flow.StateFlow

interface VideoService {
    val sharing: StateFlow<Boolean>
    val shareTargetName: StateFlow<String>
    val watching: StateFlow<Boolean>
    val streamingPeers: StateFlow<List<String>>
    val frames: StateFlow<Map<String, ImageBitmap>>
    val streamStats: StateFlow<StreamStats>
    val systemAudioAvailable: Boolean

    fun joinStream(mixerHandle: Long)
    fun leaveStream(mixerHandle: Long)
    fun startSharing(target: CaptureTarget, quality: Int, fps: Int, shareAudio: Boolean, bitrateKbps: Int, gopSize: Int): Boolean
    fun stopSharing()
    fun setStreamVolume(vol: Float)
    fun getStreamVolume(): Float
    fun listCaptureTargets(): List<CaptureTarget>
    fun grabThumbnail(target: CaptureTarget): ImageBitmap?
    fun destroy()
}