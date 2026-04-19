package com.driscord.data.api

import com.github.javakeyring.BackendNotSupportedException
import com.github.javakeyring.Keyring
import com.github.javakeyring.PasswordAccessException
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

@Serializable
data class SessionData(
    val accessToken: String,
    val refreshToken: String,
    val username: String,
)

internal object SessionStore {
    private const val SERVICE = "driscord"
    private const val ACCOUNT = "session"
    private val json = Json { ignoreUnknownKeys = true }

    private fun keyring(): Keyring? = try {
        Keyring.create()
    } catch (_: BackendNotSupportedException) {
        println("[session] OS keyring unavailable — session will not be persisted")
        null
    }

    fun load(): SessionData? = try {
        val raw = keyring()?.getPassword(SERVICE, ACCOUNT) ?: return null
        json.decodeFromString<SessionData>(raw)
    } catch (_: PasswordAccessException) {
        null
    } catch (_: Exception) {
        null
    }

    fun save(session: SessionData) {
        try {
            keyring()?.setPassword(SERVICE, ACCOUNT, json.encodeToString(session))
        } catch (_: Exception) {
            println("[session] failed to save session to OS keyring")
        }
    }

    fun clear() {
        try {
            keyring()?.deletePassword(SERVICE, ACCOUNT)
        } catch (_: PasswordAccessException) {
            // already absent — fine
        } catch (_: Exception) {}
    }
}
