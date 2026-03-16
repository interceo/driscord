package com.driscord

object NativeAudioMixer {
    init { NativeLoader.load() }

    @JvmStatic external fun create(): Long
    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun start(h: Long): Boolean
    @JvmStatic external fun stop(h: Long)

    @JvmStatic external fun deafened(h: Long): Boolean
    @JvmStatic external fun setDeafened(h: Long, deaf: Boolean)
    @JvmStatic external fun outputVolume(h: Long): Float
    @JvmStatic external fun setOutputVolume(h: Long, vol: Float)
    @JvmStatic external fun outputLevel(h: Long): Float

    @JvmStatic external fun addSource(h: Long, receiverHandle: Long)
    @JvmStatic external fun removeSource(h: Long, receiverHandle: Long)
}
