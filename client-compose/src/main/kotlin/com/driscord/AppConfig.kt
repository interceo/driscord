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
    @SerialName("server") val server: String = "localhost:9001",
    @SerialName("api") val api: String = "localhost:9002",
    @SerialName("screen_fps") val screenFps: Int = 60,
    @SerialName("capture_width") val captureWidth: Int = 1920,
    @SerialName("capture_height") val captureHeight: Int = 1080,
    @SerialName("noise_gate_threshold") val noiseGateThreshold: Float = 0.01f,
    @SerialName("mic_device_id") val micDeviceId: String = "",
    @SerialName("output_device_id") val outputDeviceId: String = "",
    @SerialName("turn_servers") val turnServers: List<TurnServerConfig> = emptyList(),
) {
    val serverUrl: String get() = "ws://${server.ifBlank { "localhost:9001" }}"
    val apiBaseUrl: String get() = "http://${api.ifBlank { "localhost:9002" }}"

    // Validation — same rules as config.cpp
    fun validated(): AppConfig =
        copy(
            server = server.ifBlank { "localhost:9001" },
            api = api.ifBlank { "localhost:9002" },
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
            // 1. Prefer on-disk config (user-editable, survives app updates)
            val file = configCandidates().firstOrNull { it.exists() && it.isFile }
            if (file != null) {
                println("[config] loaded from ${file.absolutePath}")
                return load(file.absolutePath)
            }
            // 2. Fall back to the bundled defaults embedded in the JAR at build time
            //    (driscord.json copied in as driscord_defaults.json by embedDefaultConfig)
            val stream = AppConfig::class.java.getResourceAsStream("/driscord_defaults.json")
            if (stream != null) {
                println("[config] loaded bundled defaults from JAR")
                return runCatching {
                    json.decodeFromString<AppConfig>(stream.bufferedReader().readText()).validated()
                }.getOrElse { AppConfig() }
            }
            println("[config] no config file found, using hardcoded defaults")
            return AppConfig()
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
