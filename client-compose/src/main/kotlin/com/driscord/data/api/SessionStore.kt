package com.driscord.data.api

import com.github.javakeyring.BackendNotSupportedException
import com.github.javakeyring.Keyring
import com.github.javakeyring.PasswordAccessException
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

@Serializable
data class SessionData(
    val refreshToken: String,
    val username: String,
    val userId: Int,
)

internal object SessionStore {
    private const val SERVICE = "driscord"
    private const val ACCOUNT = "session"
    private val json = Json { ignoreUnknownKeys = true }

    private val fallbackFile: File get() {
        val base = System.getenv("XDG_CONFIG_HOME")
            ?.let { File(it) }
            ?: File(System.getProperty("user.home"), ".config")
        return File(base, "driscord/session.json")
    }

    private fun keyring(): Keyring? = try {
        Keyring.create()
    } catch (_: BackendNotSupportedException) {
        null
    }

    fun load(): SessionData? {
        val raw = runCatching {
            keyring()?.getPassword(SERVICE, ACCOUNT)
        }.getOrNull() ?: runCatching {
            fallbackFile.takeIf { it.exists() }?.readText()
        }.getOrNull() ?: return null
        return runCatching { json.decodeFromString<SessionData>(raw) }.getOrNull()
    }

    fun save(session: SessionData) {
        val raw = json.encodeToString(session)
        val keyringOk = runCatching {
            val k = keyring() ?: return@runCatching false
            k.setPassword(SERVICE, ACCOUNT, raw)
            true
        }.getOrElse { false }

        if (!keyringOk) {
            runCatching {
                fallbackFile.parentFile?.mkdirs()
                fallbackFile.writeText(raw)
            }.onFailure { println("[session] failed to persist session: ${it.message}") }
        }
    }

    fun clear() {
        runCatching { keyring()?.deletePassword(SERVICE, ACCOUNT) }
        runCatching { if (fallbackFile.exists()) fallbackFile.delete() }
    }
}
