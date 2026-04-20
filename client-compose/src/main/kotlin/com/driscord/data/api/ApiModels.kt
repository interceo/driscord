package com.driscord.data.api

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

@Serializable
data class LoginRequest(val username: String, val password: String)

@Serializable
data class RegisterRequest(val username: String, val email: String, val password: String)

@Serializable
data class RefreshRequest(@SerialName("refresh_token") val refreshToken: String)

@Serializable
data class TokenResponse(
    @SerialName("access_token") val accessToken: String,
    @SerialName("refresh_token") val refreshToken: String,
)

// ---------------------------------------------------------------------------
// Servers
// ---------------------------------------------------------------------------

@Serializable
data class ServerResponse(
    val id: Int,
    val name: String,
    val description: String? = null,
    @SerialName("owner_id") val ownerId: Int,
)

@Serializable
data class CreateServerRequest(val name: String, val description: String? = null)

// ---------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------

@Serializable
data class ChannelResponse(
    val id: Int,
    @SerialName("server_id") val serverId: Int,
    val name: String,
    val kind: String,
    val position: Int,
)

@Serializable
data class CreateChannelRequest(val name: String, val kind: String, val position: Int = 0)

// ---------------------------------------------------------------------------
// User profile
// ---------------------------------------------------------------------------

@Serializable
data class UserProfileResponse(
    val id: Int,
    val username: String,
    val email: String,
    @SerialName("display_name") val displayName: String? = null,
    @SerialName("avatar_url") val avatarUrl: String? = null,
)

@Serializable
data class UpdateProfileRequest(
    @SerialName("display_name") val displayName: String? = null,
)

// ---------------------------------------------------------------------------
// Invites
// ---------------------------------------------------------------------------

@Serializable
data class InviteResponse(
    val code: String,
    @SerialName("server_id") val serverId: Int,
    @SerialName("creator_id") val creatorId: Int,
)

@Serializable
data class InviteAcceptResponse(
    @SerialName("server_id") val serverId: Int,
    val status: String,
)
