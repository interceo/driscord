package com.driscord

object NativeScreenSession {
    init { NativeLoader.load() }

    fun interface OnFrameCallback {
        fun invoke(peerId: String, rgba: ByteArray, w: Int, h: Int)
    }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    @JvmStatic external fun create(
        bufMs: Int, maxSyncMs: Int, holdMs: Int, drainMs: Int,
        videoTransportHandle: Long, audioTransportHandle: Long
    ): Long
    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun startSharing(
        h: Long, targetJson: String,
        maxW: Int, maxH: Int, fps: Int, bitrateKbps: Int, shareAudio: Boolean
    ): Boolean
    @JvmStatic external fun stopSharing(h: Long)
    @JvmStatic external fun sharing(h: Long): Boolean
    @JvmStatic external fun sharingAudio(h: Long): Boolean
    @JvmStatic external fun forceKeyframe(h: Long)

    @JvmStatic external fun update(h: Long)
    @JvmStatic external fun activePeer(h: Long): String
    @JvmStatic external fun active(h: Long): Boolean
    @JvmStatic external fun reset(h: Long)

    @JvmStatic external fun addAudioReceiverToMixer(screenHandle: Long, mixerHandle: Long)
    @JvmStatic external fun removeAudioReceiverFromMixer(screenHandle: Long, mixerHandle: Long)
    @JvmStatic external fun resetAudioReceiver(h: Long)
    @JvmStatic external fun setStreamVolume(h: Long, vol: Float)
    @JvmStatic external fun streamVolume(h: Long): Float

    /** Returns JSON with width/height/measuredKbps/video{queue,bufMs,drops,misses}/audio{...} */
    @JvmStatic external fun stats(h: Long): String

    @JvmStatic external fun setOnFrame(h: Long, cb: OnFrameCallback)
    @JvmStatic external fun setOnFrameRemoved(h: Long, cb: StringCallback)
}
