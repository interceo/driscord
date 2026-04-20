package com.driscord.data.api

import com.driscord.domain.model.UserProfile

interface UserRepository {
    suspend fun getProfile(): Result<UserProfile>
    suspend fun updateProfile(displayName: String?): Result<UserProfile>
    suspend fun uploadAvatar(userId: Int, bytes: ByteArray, extension: String): Result<UserProfile>
    suspend fun getUserByUsername(username: String): Result<UserProfile>
}
