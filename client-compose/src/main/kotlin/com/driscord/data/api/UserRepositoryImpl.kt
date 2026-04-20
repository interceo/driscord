package com.driscord.data.api

import com.driscord.domain.model.UserProfile

class UserRepositoryImpl(private val client: ApiClient) : UserRepository {

    override suspend fun getProfile(): Result<UserProfile> =
        client.get("/users/me", UserProfileResponse.serializer()).map { it.toDomain() }

    override suspend fun updateProfile(displayName: String?): Result<UserProfile> =
        client.patch(
            "/users/me",
            UpdateProfileRequest(displayName = displayName),
            UpdateProfileRequest.serializer(),
            UserProfileResponse.serializer(),
        ).map { it.toDomain() }

    override suspend fun uploadAvatar(userId: Int, bytes: ByteArray, extension: String): Result<UserProfile> {
        val mimeType = when (extension.lowercase()) {
            "jpg", "jpeg" -> "image/jpeg"
            "png"         -> "image/png"
            "gif"         -> "image/gif"
            "webp"        -> "image/webp"
            else          -> "image/jpeg"
        }
        return client.putMultipart(
            "/users/$userId/avatar",
            "file",
            "avatar.$extension",
            bytes,
            mimeType,
            UserProfileResponse.serializer(),
        ).map { it.toDomain() }
    }

    override suspend fun getUserByUsername(username: String): Result<UserProfile> {
        val encoded = java.net.URLEncoder.encode(username, "UTF-8")
        return client.get("/users/lookup?username=$encoded", UserProfileResponse.serializer()).map { it.toDomain() }
    }

    private fun UserProfileResponse.toDomain() = UserProfile(
        id          = id,
        username    = username,
        email       = email,
        displayName = displayName,
        avatarUrl   = avatarUrl,
    )
}
