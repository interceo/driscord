package com.driscord.jni

object NativeVideoTransport {
    init {
        NativeLoader.load()
    }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    @JvmStatic external fun create(transportHandle: Long): Long

    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun setWatching(
        h: Long,
        watching: Boolean,
    )

    @JvmStatic external fun watching(h: Long): Boolean

    @JvmStatic external fun removeStreamingPeer(
        h: Long,
        peer: String,
    )

    @JvmStatic external fun sendKeyframeRequest(h: Long)

    /** Fires once per newly-seen streaming peer id. */
    @JvmStatic external fun setOnNewStreamingPeer(
        h: Long,
        cb: StringCallback,
    )

    /** Fires when a streaming peer is removed via removeStreamingPeer(). */
    @JvmStatic external fun setOnStreamingPeerRemoved(
        h: Long,
        cb: StringCallback,
    )
}
