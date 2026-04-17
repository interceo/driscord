package com.driscord

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

// ---------------------------------------------------------------------------
// Data model — mirrors Config / TurnServer from config.hpp
// ---------------------------------------------------------------------------

@Serializable
data class TurnServerConfig(
    val url: String = "",
    val user: String = "",
    val pass: String = "",
)

@Serializable
data class AppConfig(
    @SerialName("server_host") val serverHost: String = "localhost",
    @SerialName("server_port") val serverPort: Int = 8080,
    @SerialName("api_host") val apiHost: String = "localhost",
    @SerialName("api_port") val apiPort: Int = 8000,
    @SerialName("screen_fps") val screenFps: Int = 60,
    @SerialName("capture_width") val captureWidth: Int = 1920,
    @SerialName("capture_height") val captureHeight: Int = 1080,
    @SerialName("noise_gate_threshold") val noiseGateThreshold: Float = 0.01f,
    @SerialName("mic_device_id") val micDeviceId: String = "",
    @SerialName("output_device_id") val outputDeviceId: String = "",
    @SerialName("turn_servers") val turnServers: List<TurnServerConfig> = emptyList(),
) {
    val serverUrl: String get() = "ws://$serverHost:$serverPort"
    val apiBaseUrl: String get() = "http://$apiHost:$apiPort"

    // Validation — same rules as config.cpp
    fun validated(): AppConfig =
        copy(
            serverHost = serverHost.ifBlank { "localhost" },
            serverPort = serverPort.coerceIn(1, 65535),
            apiHost = apiHost.ifBlank { "localhost" },
            apiPort = apiPort.coerceIn(1, 65535),
            screenFps = screenFps.coerceIn(1, 240),
            noiseGateThreshold = noiseGateThreshold.coerceIn(0f, 1f),
        )

    companion object {
        private val json =
            Json {
                ignoreUnknownKeys = true
                isLenient = true
                prettyPrint = true
            }

        /**
         * Loads config from an explicit path.
         * Returns defaults on missing file or parse error.
         */
        fun load(path: String): AppConfig =
            runCatching {
                val text = File(path).readText()
                json.decodeFromString<AppConfig>(text).validated()
            }.getOrElse { AppConfig() }

        /**
         * Mirrors Config::load_default() from config.cpp:
         *
         * Windows:
         *   1. <exe dir>\driscord.json
         *   2. %LOCALAPPDATA%\driscord\config.json
         *
         * Linux / macOS:
         *   1. ./driscord.json
         *   2. ~/.config/driscord/config.json
         */
        fun loadDefault(): AppConfig {
            val file = configCandidates().firstOrNull { it.exists() && it.isFile }
            return if (file != null) {
                println("[config] loaded from ${file.absolutePath}")
                load(file.absolutePath)
            } else {
                println("[config] no config file found, using defaults")
                AppConfig()
            }
        }

        /**
         * Returns the path that loadDefault() would use (the first candidate
         * that exists, or the primary candidate for saving if none exist yet).
         */
        fun defaultConfigPath(): String =
            (configCandidates().firstOrNull { it.exists() } ?: configCandidates().first()).absolutePath

        private fun configCandidates(): List<File> = buildList {
            add(File("driscord.json"))
            val isWindows = System.getProperty("os.name").lowercase().contains("win")
            if (isWindows) {
                val appData = System.getenv("LOCALAPPDATA")
                if (!appData.isNullOrBlank()) add(File(appData, "driscord/config.json"))
            } else {
                val xdgConfig = System.getenv("XDG_CONFIG_HOME")
                val configDir = if (!xdgConfig.isNullOrBlank()) File(xdgConfig)
                                else File(System.getProperty("user.home"), ".config")
                add(File(configDir, "driscord/config.json"))
            }
        }

        /** Writes config as JSON to the given path, creating parent dirs as needed. */
        fun save(
            config: AppConfig,
            path: String,
        ) {
            val file = File(path)
            file.parentFile?.mkdirs()
            file.writeText(json.encodeToString(config.validated()))
        }
    }
}
