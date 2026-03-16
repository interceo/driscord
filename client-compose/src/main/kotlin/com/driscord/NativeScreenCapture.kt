package com.driscord

object NativeScreenCapture {
    init { NativeLoader.load() }

    @JvmStatic external fun systemAudioAvailable(): Boolean

    /** Returns JSON array of CaptureTarget objects. */
    @JvmStatic external fun listTargets(): String

    /** Returns RGBA bytes, or null if no frame could be grabbed. */
    @JvmStatic external fun grabThumbnail(targetJson: String, maxW: Int, maxH: Int): ByteArray?
}
