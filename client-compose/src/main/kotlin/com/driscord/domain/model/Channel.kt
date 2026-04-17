package com.driscord.domain.model

enum class ChannelKind { voice, text }

data class Channel(
    val id: Int,
    val serverId: Int,
    val name: String,
    val kind: ChannelKind,
    val position: Int,
)
