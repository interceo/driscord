package com.driscord.data.api

import com.driscord.domain.model.UserProfile

interface UserRepository {
    suspend fun getUserById(id: Int): Result<UserProfile>
    suspend fun updateProfile(userId: Int, displayName: String?): Result<UserProfile>
    suspend fun uploadAvatar(userId: Int, bytes: ByteArray, extension: String): Result<UserProfile>
    suspend fun getUserByUsername(username: String): Result<UserProfile>
}
