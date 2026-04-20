package com.driscord.data.api

class AuthRepositoryImpl(private val client: ApiClient) : AuthRepository {

    private var _username: String? = null
    private var _userId: Int? = null
    private var refreshToken: String? = null

    override val isLoggedIn: Boolean get() = refreshToken != null
    override val currentUsername: String? get() = _username
    override val currentUserId: Int? get() = _userId

    init {
        SessionStore.load()?.let { session ->
            refreshToken = session.refreshToken
            _username = session.username
            _userId = session.userId
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
            _userId = tokens.userId
            SessionStore.save(SessionData(tokens.refreshToken, username, tokens.userId))
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
            _userId = tokens.userId
            SessionStore.save(SessionData(tokens.refreshToken, username, tokens.userId))
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
            _userId = tokens.userId
            SessionStore.save(SessionData(tokens.refreshToken, _username ?: "", tokens.userId))
        }
    }

    override fun logout() {
        client.accessToken = null
        refreshToken = null
        _username = null
        SessionStore.clear()
    }
}
