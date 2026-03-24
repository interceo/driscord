package com.driscord.jni

object NativeScreenSession {
    init {
        NativeLoader.load()
    }

    fun interface OnFrameCallback {
        fun invoke(
            peerId: String,
            rgba: ByteArray,
            w: Int,
            h: Int,
        )
    }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    @JvmStatic external fun init(bufMs: Int, maxSyncMs: Int)

    @JvmStatic external fun deinit()

    @JvmStatic external fun startSharing(
        targetJson: String,
        maxW: Int,
        maxH: Int,
        fps: Int,
        bitrateKbps: Int,
        gopSize: Int,
        shareAudio: Boolean,
    ): Boolean

    @JvmStatic external fun stopSharing()

    @JvmStatic external fun sharing(): Boolean

    @JvmStatic external fun sharingAudio(): Boolean

    @JvmStatic external fun forceKeyframe()

    @JvmStatic external fun update()

    @JvmStatic external fun activePeer(): String

    @JvmStatic external fun active(): Boolean

    @JvmStatic external fun reset()

    @JvmStatic external fun resetAudioReceiver()

    @JvmStatic external fun setStreamVolume(peerId: String, vol: Float)

    @JvmStatic external fun streamVolume(): Float

    /** Returns JSON with width/height/measuredKbps/video{queue,bufMs,drops,misses}/audio{...} */
    @JvmStatic external fun stats(): String

    @JvmStatic external fun setOnFrame(cb: OnFrameCallback)

    @JvmStatic external fun setOnFrameRemoved(cb: StringCallback)

    @JvmStatic external fun joinStream(peerId: String)

    @JvmStatic external fun leaveStream()
}
