package com.driscord.jni

object NativeDriscord {
    init {
        NativeLoader.load()
    }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    fun interface OnFrameCallback {
        fun invoke(
            peerId: String,
            rgba: ByteArray,
            w: Int,
            h: Int,
        )
    }

    // -- Transport --

    @JvmStatic external fun addTurnServer(url: String, user: String, pass: String)

    @JvmStatic external fun connect(url: String): String?

    @JvmStatic external fun disconnect()

    @JvmStatic external fun connected(): Boolean

    @JvmStatic external fun localId(): String

    /** Returns JSON array of {id: String, connected: Boolean} */
    @JvmStatic external fun peers(): String

    @JvmStatic external fun setOnPeerJoined(cb: StringCallback)

    @JvmStatic external fun setOnPeerLeft(cb: StringCallback)

    @JvmStatic external fun setOnStreamingStarted(cb: StringCallback)

    @JvmStatic external fun setOnStreamingStopped(cb: StringCallback)

    // -- Audio --

    @JvmStatic external fun audioSend(data: ByteArray, len: Int)

    @JvmStatic external fun audioStart(voiceBitrateKbps: Int): String?

    @JvmStatic external fun audioStop()

    @JvmStatic external fun audioDeafened(): Boolean

    @JvmStatic external fun audioSetDeafened(deaf: Boolean)

    @JvmStatic external fun audioMasterVolume(): Float

    @JvmStatic external fun audioSetMasterVolume(vol: Float)

    @JvmStatic external fun audioOutputLevel(): Float

    @JvmStatic external fun audioSelfMuted(): Boolean

    @JvmStatic external fun audioSetSelfMuted(muted: Boolean)

    @JvmStatic external fun audioInputLevel(): Float

    @JvmStatic external fun audioSetNoiseGate(threshold: Float)

    /** Returns JSON array of {id, name} for all capture (mic) devices. */
    @JvmStatic external fun audioListInputDevices(): String

    /** Set the mic capture device by id (name). Empty string = system default. */
    @JvmStatic external fun audioSetInputDevice(id: String)

    /** Returns JSON array of {id, name} for all playback (output) devices. */
    @JvmStatic external fun audioListOutputDevices(): String

    /** Set the audio output device by id (name). Empty string = system default. */
    @JvmStatic external fun audioSetOutputDevice(id: String)

    @JvmStatic external fun audioOnPeerJoined(peer: String, jitterMs: Int)

    @JvmStatic external fun audioOnPeerLeft(peer: String)

    @JvmStatic external fun audioSetPeerVolume(peer: String, vol: Float)

    @JvmStatic external fun audioGetPeerVolume(peer: String): Float

    @JvmStatic external fun audioSetPeerMuted(peer: String, muted: Boolean)

    @JvmStatic external fun audioGetPeerMuted(peer: String): Boolean

    @JvmStatic external fun audioSetScreenAudioReceiver(peerId: String, screenHandle: Long)

    @JvmStatic external fun audioUnsetScreenAudioReceiver(peerId: String)

    @JvmStatic external fun audioAddScreenAudioToMixer(peerId: String)

    @JvmStatic external fun audioRemoveScreenAudioFromMixer(peerId: String)

    // -- Video --

    @JvmStatic external fun videoSetWatching(watching: Boolean)

    @JvmStatic external fun videoWatching(): Boolean

    @JvmStatic external fun videoRemoveStreamingPeer(peer: String)

    @JvmStatic external fun videoSendKeyframeRequest()

    @JvmStatic external fun setOnNewStreamingPeer(cb: StringCallback)

    @JvmStatic external fun setOnStreamingPeerRemoved(cb: StringCallback)

    // -- Capture --

    @JvmStatic external fun captureSystemAudioAvailable(): Boolean

    /** Returns JSON array of CaptureTarget objects. */
    @JvmStatic external fun captureVideoListTargets(): String

    /** Returns RGBA bytes, or null if no frame could be grabbed. */
    @JvmStatic external fun captureGrabThumbnail(
        targetJson: String,
        maxW: Int,
        maxH: Int,
    ): ByteArray?

    // -- Screen --

    @JvmStatic external fun screenInit(bufMs: Int, maxSyncMs: Int)

    @JvmStatic external fun screenSetSystemAudioBitrate(kbps: Int)

    @JvmStatic external fun screenDeinit()

    @JvmStatic external fun screenStartSharing(
        targetJson: String,
        maxW: Int,
        maxH: Int,
        fps: Int,
        bitrateKbps: Int,
        gopSize: Int,
        shareAudio: Boolean,
    ): String?

    @JvmStatic external fun screenStopSharing()

    @JvmStatic external fun screenSharing(): Boolean

    @JvmStatic external fun screenSharingAudio(): Boolean

    @JvmStatic external fun screenForceKeyframe()

    @JvmStatic external fun screenUpdate()

    @JvmStatic external fun screenActivePeer(): String

    @JvmStatic external fun screenActive(): Boolean

    @JvmStatic external fun screenReset()

    @JvmStatic external fun screenResetAudioReceiver()

    @JvmStatic external fun screenSetStreamVolume(peerId: String, vol: Float)

    @JvmStatic external fun screenStreamVolume(): Float

    /** Returns JSON with width/height/measuredKbps/video{queue,bufMs,drops,misses}/audio{...} */
    @JvmStatic external fun screenStats(): String

    @JvmStatic external fun setOnFrame(cb: OnFrameCallback)

    @JvmStatic external fun setOnFrameRemoved(cb: StringCallback)

    @JvmStatic external fun screenJoinStream(peerId: String)

    @JvmStatic external fun screenLeaveStream()
}
