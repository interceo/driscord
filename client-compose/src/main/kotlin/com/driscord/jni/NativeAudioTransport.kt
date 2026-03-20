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

    @JvmStatic external fun setScreenAudioReceiver(
        audioHandle: Long,
        peerId: String,
        screenHandle: Long,
    )

    @JvmStatic external fun unsetScreenAudioReceiver(
        audioHandle: Long,
        peerId: String,
    )
}
