package com.driscord.data.api

class AuthRepositoryImpl(private val client: ApiClient) : AuthRepository {

    private var _username: String? = null
    private var refreshToken: String? = null

    override val isLoggedIn: Boolean get() = refreshToken != null
    override val currentUsername: String? get() = _username

    init {
        SessionStore.load()?.let { session ->
            refreshToken = session.refreshToken
            _username = session.username
            println("[session] restored for ${session.username}")
        }
    }

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
            SessionStore.save(SessionData(tokens.refreshToken, username))
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
            SessionStore.save(SessionData(tokens.refreshToken, username))
        }
    }

    override suspend fun refreshSession(): Result<Unit> {
        val token = refreshToken ?: return Result.failure(Exception("no refresh token"))
        val result = client.post(
            "/auth/refresh",
            RefreshRequest(token),
            RefreshRequest.serializer(),
            TokenResponse.serializer(),
        )
        return result.map { tokens ->
            client.accessToken = tokens.accessToken
            refreshToken = tokens.refreshToken
            SessionStore.save(SessionData(tokens.refreshToken, _username ?: ""))
        }
    }

    override fun logout() {
        client.accessToken = null
        refreshToken = null
        _username = null
        SessionStore.clear()
    }
}
