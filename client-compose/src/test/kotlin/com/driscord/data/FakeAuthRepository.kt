package com.driscord.data

import com.driscord.data.api.AuthRepository

class FakeAuthRepository : AuthRepository {

    var loginResult: Result<Unit> = Result.success(Unit)
    var registerResult: Result<Unit> = Result.success(Unit)

    var loginCalls = mutableListOf<Pair<String, String>>()
    var registerCalls = mutableListOf<Triple<String, String, String>>()
    var logoutCalled = 0

    private var _username: String? = null
    private var _loggedIn = false

    override val isLoggedIn: Boolean get() = _loggedIn
    override val currentUsername: String? get() = _username

    override suspend fun login(username: String, password: String): Result<Unit> {
        loginCalls += Pair(username, password)
        loginResult.onSuccess { _loggedIn = true; _username = username }
        return loginResult
    }

    override suspend fun register(username: String, email: String, password: String): Result<Unit> {
        registerCalls += Triple(username, email, password)
        registerResult.onSuccess { _loggedIn = true; _username = username }
        return registerResult
    }

    override fun logout() {
        logoutCalled++
        _loggedIn = false
        _username = null
    }
}
