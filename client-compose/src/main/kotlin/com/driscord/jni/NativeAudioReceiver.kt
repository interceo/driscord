package com.driscord.jni

object NativeAudioReceiver {
    init { NativeLoader.load() }

    @JvmStatic external fun create(jitterMs: Int): Long
    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun reset(h: Long)
    @JvmStatic external fun setVolume(h: Long, vol: Float)
    @JvmStatic external fun volume(h: Long): Float
}
