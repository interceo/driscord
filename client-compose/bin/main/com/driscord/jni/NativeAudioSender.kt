package com.driscord.jni

object NativeAudioSender {
    init {
        NativeLoader.load()
    }

    @JvmStatic external fun create(audioTransportHandle: Long): Long

    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun start(h: Long): Boolean

    @JvmStatic external fun stop(h: Long)

    @JvmStatic external fun muted(h: Long): Boolean

    @JvmStatic external fun setMuted(
        h: Long,
        muted: Boolean,
    )

    @JvmStatic external fun inputLevel(h: Long): Float
}
