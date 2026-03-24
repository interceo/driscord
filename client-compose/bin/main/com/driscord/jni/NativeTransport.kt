package com.driscord.jni

object NativeTransport {
    init {
        NativeLoader.load()
    }

    fun interface StringCallback {
        fun invoke(peerId: String)
    }

    @JvmStatic external fun addTurnServer(url: String, user: String, pass: String)

    @JvmStatic external fun connect(url: String)

    @JvmStatic external fun disconnect()

    @JvmStatic external fun connected(): Boolean

    @JvmStatic external fun localId(): String

    /** Returns JSON array of {id: String, connected: Boolean} */
    @JvmStatic external fun peers(): String

    @JvmStatic external fun setOnPeerJoined(cb: StringCallback)

    @JvmStatic external fun setOnPeerLeft(cb: StringCallback)
}
