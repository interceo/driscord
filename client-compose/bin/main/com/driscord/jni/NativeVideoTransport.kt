package com.driscord.jni

object NativeVideoTransport {
    init {
        NativeLoader.load()
    }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    @JvmStatic external fun setWatching(watching: Boolean)

    @JvmStatic external fun watching(): Boolean

    @JvmStatic external fun removeStreamingPeer(peer: String)

    @JvmStatic external fun sendKeyframeRequest()

    /** Fires once per newly-seen streaming peer id. */
    @JvmStatic external fun setOnNewStreamingPeer(cb: StringCallback)

    /** Fires when a streaming peer is removed via removeStreamingPeer(). */
    @JvmStatic external fun setOnStreamingPeerRemoved(cb: StringCallback)
}
