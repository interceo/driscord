package com.driscord

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
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
    @SerialName("server_host")       val serverHost: String         = "localhost",
    @SerialName("server_port")       val serverPort: Int            = 8080,
    @SerialName("screen_fps")        val screenFps: Int             = 60,
    @SerialName("capture_width")     val captureWidth: Int          = 1920,
    @SerialName("capture_height")    val captureHeight: Int         = 1080,
    @SerialName("video_bitrate_kbps") val videoBitrateKbps: Int     = 8000,
    @SerialName("voice_jitter_ms")   val voiceJitterMs: Int         = 80,
    @SerialName("screen_buffer_ms")  val screenBufferMs: Int        = 120,
    @SerialName("max_sync_gap_ms")   val maxSyncGapMs: Int          = 2000,
    @SerialName("turn_servers")      val turnServers: List<TurnServerConfig> = emptyList(),
) {
    val serverUrl: String get() = "ws://$serverHost:$serverPort"

    // Validation — same rules as config.cpp
    fun validated(): AppConfig = copy(
        serverHost       = serverHost.ifBlank { "localhost" },
        serverPort       = serverPort.coerceIn(1, 65535),
        screenFps        = screenFps.coerceIn(1, 240),
        videoBitrateKbps = videoBitrateKbps.coerceIn(100, 100_000),
        voiceJitterMs    = voiceJitterMs.coerceIn(0, 500),
        screenBufferMs   = screenBufferMs.coerceIn(0, 500),
        maxSyncGapMs     = maxSyncGapMs.coerceIn(100, 10_000),
    )

    companion object {
        private val json = Json {
            ignoreUnknownKeys = true
            isLenient = true
        }

        /**
         * Loads config from an explicit path.
         * Returns defaults on missing file or parse error.
         */
        fun load(path: String): AppConfig = runCatching {
            val text = File(path).readText()
            json.decodeFromString<AppConfig>(text).validated()
        }.getOrElse { AppConfig() }

        /**
         * Mirrors Config::load_default() from config.cpp:
         *
         * Windows:
         *   1. <exe dir>\driscord.json
         *   2. %APPDATA%\driscord\config.json
         *
         * Linux / macOS:
         *   1. ./driscord.json
         *   2. ~/.config/driscord/config.json
         */
        fun loadDefault(): AppConfig {
            val isWindows = System.getProperty("os.name").lowercase().contains("win")

            val candidates: List<File> = buildList {
                // 1. Next to the JAR / working directory
                add(File("driscord.json"))

                if (isWindows) {
                    // 2. %APPDATA%\driscord\config.json
                    val appData = System.getenv("APPDATA")
                    if (!appData.isNullOrBlank()) {
                        add(File(appData, "driscord/config.json"))
                    }
                } else {
                    // 2. ~/.config/driscord/config.json  (XDG)
                    val xdgConfig = System.getenv("XDG_CONFIG_HOME")
                    val configDir = if (!xdgConfig.isNullOrBlank()) {
                        File(xdgConfig)
                    } else {
                        File(System.getProperty("user.home"), ".config")
                    }
                    add(File(configDir, "driscord/config.json"))
                }
            }

            for (f in candidates) {
                if (f.exists() && f.isFile) {
                    println("[config] loaded from ${f.absolutePath}")
                    return load(f.absolutePath)
                }
            }

            println("[config] no config file found, using defaults")
            return AppConfig()
        }
    }
}
