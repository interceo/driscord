package com.driscord.domain.model

import kotlinx.serialization.Serializable

@Serializable
data class PeerInfo(
    val id: String,
    val connected: Boolean,
)