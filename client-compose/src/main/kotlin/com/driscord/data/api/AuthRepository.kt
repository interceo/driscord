package com.driscord.data.api

interface AuthRepository {
    val isLoggedIn: Boolean
    val currentUsername: String?

    suspend fun login(username: String, password: String): Result<Unit>
    suspend fun register(username: String, email: String, password: String): Result<Unit>
    fun logout()
}
