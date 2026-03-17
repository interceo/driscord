package com.driscord

import kotlinx.serialization.Serializable

enum class AppState { Disconnected, Connecting, Connected }

@Serializable
data class PeerInfo(val id: String, val connected: Boolean)

@Serializable
data class CaptureTarget(
    val type: Int,   // 0 = Monitor, 1 = Window
    val id: String,
    val name: String,
    val width: Int,
    val height: Int,
    val x: Int,
    val y: Int,
)

@Serializable
data class StreamStats(
    val width: Int = 0,
    val height: Int = 0,
    val measuredKbps: Int = 0,
    val video: JitterStats = JitterStats(),
    val audio: JitterStats = JitterStats(),
) {
    @Serializable
    data class JitterStats(
        val queue: Int = 0,
        val bufMs: Int = 0,
        val drops: Long = 0,
        val misses: Long = 0,
    )
}
