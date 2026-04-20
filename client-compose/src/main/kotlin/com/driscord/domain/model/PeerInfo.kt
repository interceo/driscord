package com.driscord.domain.model

import kotlinx.serialization.Serializable

@Serializable
data class PeerInfo(
    val id: String,
    val connected: Boolean,
    val username: String = "",
    val userId: Int? = null,
    val displayName: String? = null,
    val avatarUrl: String? = null,
)
