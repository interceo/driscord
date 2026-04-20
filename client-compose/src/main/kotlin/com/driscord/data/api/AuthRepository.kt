package com.driscord.data.api

interface AuthRepository {
    val isLoggedIn: Boolean
    val currentUsername: String?
    val currentUserId: Int?

    suspend fun login(username: String, password: String): Result<Unit>
    suspend fun register(username: String, email: String, password: String): Result<Unit>
    suspend fun refreshSession(): Result<Unit>
    fun logout()
}
