package com.driscord.domain.model

data class UserProfile(
    val id: Int,
    val username: String,
    val email: String,
    val displayName: String?,
    val avatarUrl: String?,
)
