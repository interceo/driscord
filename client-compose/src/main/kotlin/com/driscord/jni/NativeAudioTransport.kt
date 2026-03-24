package com.driscord.jni

object NativeAudioTransport {
    init {
        NativeLoader.load()
    }

    @JvmStatic external fun sendAudio(data: ByteArray, len: Int)

    @JvmStatic external fun start(): Boolean

    @JvmStatic external fun stop()

    @JvmStatic external fun deafened(): Boolean

    @JvmStatic external fun setDeafened(deaf: Boolean)

    @JvmStatic external fun masterVolume(): Float

    @JvmStatic external fun setMasterVolume(vol: Float)

    @JvmStatic external fun outputLevel(): Float

    @JvmStatic external fun selfMuted(): Boolean

    @JvmStatic external fun setSelfMuted(muted: Boolean)

    @JvmStatic external fun inputLevel(): Float

    @JvmStatic external fun onPeerJoined(peer: String, jitterMs: Int)

    @JvmStatic external fun onPeerLeft(peer: String)

    @JvmStatic external fun setPeerVolume(peer: String, vol: Float)

    @JvmStatic external fun getPeerVolume(peer: String): Float

    @JvmStatic external fun setPeerMuted(peer: String, muted: Boolean)

    @JvmStatic external fun getPeerMuted(peer: String): Boolean

    @JvmStatic external fun setScreenAudioReceiver(peerId: String, screenHandle: Long)

    @JvmStatic external fun unsetScreenAudioReceiver(peerId: String)

    @JvmStatic external fun addScreenAudioToMixer(peerId: String)

    @JvmStatic external fun removeScreenAudioFromMixer(peerId: String)
}
