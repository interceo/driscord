package com.driscord.data.api

class AuthRepositoryImpl(private val client: ApiClient) : AuthRepository {

    private var _username: String? = null
    private var refreshToken: String? = null

    override val isLoggedIn: Boolean get() = client.accessToken != null
    override val currentUsername: String? get() = _username

    override suspend fun login(username: String, password: String): Result<Unit> {
        val result = client.post(
            "/auth/login",
            LoginRequest(username, password),
            LoginRequest.serializer(),
            TokenResponse.serializer(),
        )
        return result.map { tokens ->
            client.accessToken = tokens.accessToken
            refreshToken = tokens.refreshToken
            _username = username
        }
    }

    override suspend fun register(username: String, email: String, password: String): Result<Unit> {
        val result = client.post(
            "/auth/register",
            RegisterRequest(username, email, password),
            RegisterRequest.serializer(),
            TokenResponse.serializer(),
        )
        return result.map { tokens ->
            client.accessToken = tokens.accessToken
            refreshToken = tokens.refreshToken
            _username = username
        }
    }

    override fun logout() {
        client.accessToken = null
        refreshToken = null
        _username = null
    }
}
