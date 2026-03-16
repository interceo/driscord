package com.driscord

/**
 * Thin JNI wrapper around the C++ Session object.
 * All methods are stateless — the session state lives in the native heap.
 */
object NativeSession {

    init {
        System.loadLibrary("driscord_jni")
    }

    // --- lifecycle ---
    @JvmStatic external fun create(
        host: String, port: Int,
        screenFps: Int, bitrateKbps: Int,
        voiceJitterMs: Int, screenBufMs: Int, maxSyncGapMs: Int
    ): Long

    @JvmStatic external fun destroy(handle: Long)

    // --- connection ---
    @JvmStatic external fun connect(handle: Long, url: String)
    @JvmStatic external fun disconnect(handle: Long)
    @JvmStatic external fun connected(handle: Long): Boolean
    @JvmStatic external fun localId(handle: Long): String

    // --- update loop ---
    @JvmStatic external fun update(handle: Long)

    // --- audio control ---
    @JvmStatic external fun startAudio(handle: Long): Boolean
    @JvmStatic external fun toggleMute(handle: Long)
    @JvmStatic external fun toggleDeafen(handle: Long)
    @JvmStatic external fun muted(handle: Long): Boolean
    @JvmStatic external fun deafened(handle: Long): Boolean
    @JvmStatic external fun setVolume(handle: Long, vol: Float)
    @JvmStatic external fun volume(handle: Long): Float
    @JvmStatic external fun inputLevel(handle: Long): Float
    @JvmStatic external fun outputLevel(handle: Long): Float
    @JvmStatic external fun setPeerVolume(handle: Long, peerId: String, vol: Float)
    @JvmStatic external fun peerVolume(handle: Long, peerId: String): Float

    // --- stream (watching) ---
    @JvmStatic external fun joinStream(handle: Long)
    @JvmStatic external fun leaveStream(handle: Long)
    @JvmStatic external fun watchingStream(handle: Long): Boolean
    @JvmStatic external fun setStreamVolume(handle: Long, vol: Float)
    @JvmStatic external fun streamVolume(handle: Long): Float

    // --- screen sharing (sending) ---
    @JvmStatic external fun startSharing(handle: Long, targetJson: String, quality: Int, fps: Int, shareAudio: Boolean): Boolean
    @JvmStatic external fun stopSharing(handle: Long)
    @JvmStatic external fun sharing(handle: Long): Boolean
    @JvmStatic external fun sharingAudio(handle: Long): Boolean
    @JvmStatic external fun systemAudioAvailable(): Boolean

    // --- peers (JSON strings) ---
    /** Returns JSON array of {id: String, connected: Boolean} */
    @JvmStatic external fun peers(handle: Long): String
    /** Returns JSON array of peer id strings that are currently streaming */
    @JvmStatic external fun streamingPeers(handle: Long): String

    // --- stats ---
    @JvmStatic external fun streamStats(handle: Long): String

    // --- capture targets ---
    @JvmStatic external fun listCaptureTargets(): String
    /** Returns RGBA bytes, or null if no frame could be grabbed */
    @JvmStatic external fun grabThumbnail(targetJson: String, maxW: Int, maxH: Int): ByteArray?

    // --- callbacks ---
    // The callback objects must implement a single @FunctionalInterface method
    // with the matching signature so that JNI can call "invoke" on them.
    @JvmStatic external fun setOnFrame(handle: Long, cb: OnFrameCallback)
    @JvmStatic external fun setOnFrameRemoved(handle: Long, cb: OnStringCallback)
    @JvmStatic external fun setOnPeerJoined(handle: Long, cb: OnStringCallback)
    @JvmStatic external fun setOnPeerLeft(handle: Long, cb: OnStringCallback)

    fun interface OnFrameCallback {
        fun invoke(peerId: String, rgba: ByteArray, w: Int, h: Int)
    }

    fun interface OnStringCallback {
        fun invoke(peerId: String)
    }
}
