package com.driscord.jni

object NativeAudioTransport {
    init {
        NativeLoader.load()
    }

    @JvmStatic external fun sendAudio(data: ByteArray, len: Int)

    @JvmStatic external fun registerVoiceReceiver(peer: String, receiverHandle: Long)

    @JvmStatic external fun unregisterVoiceReceiver(peer: String)

    @JvmStatic external fun setScreenAudioReceiver(peerId: String, screenHandle: Long)

    @JvmStatic external fun unsetScreenAudioReceiver(peerId: String)
}
