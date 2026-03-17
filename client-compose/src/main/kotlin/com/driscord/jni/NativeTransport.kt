package com.driscord.jni

object NativeTransport {
    init { NativeLoader.load() }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    @JvmStatic external fun create(): Long
    @JvmStatic external fun destroy(h: Long)

    @JvmStatic external fun addTurnServer(h: Long, url: String, user: String, pass: String)
    @JvmStatic external fun connect(h: Long, url: String)
    @JvmStatic external fun disconnect(h: Long)
    @JvmStatic external fun connected(h: Long): Boolean
    @JvmStatic external fun localId(h: Long): String

    /** Returns JSON array of {id: String, connected: Boolean} */
    @JvmStatic external fun peers(h: Long): String

    @JvmStatic external fun setOnPeerJoined(h: Long, cb: StringCallback)
    @JvmStatic external fun setOnPeerLeft(h: Long, cb: StringCallback)
}
