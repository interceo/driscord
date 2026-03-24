package com.driscord.domain.model

import kotlinx.serialization.Serializable

@Serializable
data class StreamStats(
    val width: Int = 0,
    val height: Int = 0,
    val measuredKbps: Int = 0,
    val video: JitterStats = JitterStats(),
    val audio: JitterStats = JitterStats(),
)

@Serializable
data class JitterStats(
    val queue: Int = 0,
    val bufMs: Int = 0,
    val drops: Long = 0,
    val misses: Long = 0,
)