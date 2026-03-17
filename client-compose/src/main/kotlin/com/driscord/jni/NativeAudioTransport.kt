package com.driscord.jni

object NativeAudioTransport {
    init {
        NativeLoader.load()
    }

    @JvmStatic external fun create(transportHandle: Long): Long

    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun sendAudio(
        h: Long,
        data: ByteArray,
        len: Int,
    )

    @JvmStatic external fun registerVoiceReceiver(
        h: Long,
        peer: String,
        receiverHandle: Long,
    )

    @JvmStatic external fun unregisterVoiceReceiver(
        h: Long,
        peer: String,
    )

    /** Pass screenHandle=0 to clear. */
    @JvmStatic external fun setScreenAudioReceiver(
        audioHandle: Long,
        screenHandle: Long,
    )
}
