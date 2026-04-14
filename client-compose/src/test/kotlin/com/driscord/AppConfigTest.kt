package com.driscord

import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.jupiter.api.io.TempDir
import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals

class AppConfigTest {

    private val json = Json {
        ignoreUnknownKeys = true
        prettyPrint = true
    }

    // -- validated() clamping --------------------------------------------------

    @Test
    fun `validated clamps port below minimum`() {
        val cfg = AppConfig(serverPort = 0).validated()
        assertEquals(1, cfg.serverPort)
    }

    @Test
    fun `validated clamps port above maximum`() {
        val cfg = AppConfig(serverPort = 70_000).validated()
        assertEquals(65535, cfg.serverPort)
    }

    @Test
    fun `validated clamps fps below minimum`() {
        val cfg = AppConfig(screenFps = 0).validated()
        assertEquals(1, cfg.screenFps)
    }

    @Test
    fun `validated clamps fps above maximum`() {
        val cfg = AppConfig(screenFps = 999).validated()
        assertEquals(240, cfg.screenFps)
    }

    @Test
    fun `validated clamps negative noise gate threshold`() {
        val cfg = AppConfig(noiseGateThreshold = -0.5f).validated()
        assertEquals(0f, cfg.noiseGateThreshold)
    }

    @Test
    fun `validated clamps noise gate threshold above 1`() {
        val cfg = AppConfig(noiseGateThreshold = 2f).validated()
        assertEquals(1f, cfg.noiseGateThreshold)
    }

    @Test
    fun `validated replaces blank host with localhost`() {
        val cfg = AppConfig(serverHost = "   ").validated()
        assertEquals("localhost", cfg.serverHost)
    }

    @Test
    fun `validated preserves valid values`() {
        val cfg = AppConfig(
            serverHost = "myserver",
            serverPort = 9090,
            screenFps = 30,
            noiseGateThreshold = 0.05f,
        )
        assertEquals(cfg, cfg.validated())
    }

    // -- serverUrl computed property -------------------------------------------

    @Test
    fun `serverUrl builds correct websocket URL`() {
        val cfg = AppConfig(serverHost = "example.com", serverPort = 4443)
        assertEquals("ws://example.com:4443", cfg.serverUrl)
    }

    // -- JSON serialization round-trip -----------------------------------------

    @Test
    fun `json round-trip preserves default config`() {
        val original = AppConfig()
        val text = json.encodeToString(original)
        val decoded = json.decodeFromString<AppConfig>(text)
        assertEquals(original, decoded)
    }

    @Test
    fun `json round-trip preserves config with turn servers`() {
        val original = AppConfig(
            turnServers = listOf(
                TurnServerConfig(url = "turn:stun.example.com", user = "u", pass = "p"),
            ),
        )
        val text = json.encodeToString(original)
        val decoded = json.decodeFromString<AppConfig>(text)
        assertEquals(original, decoded)
    }

    @Test
    fun `json with unknown keys decodes successfully`() {
        val text = """{"server_host":"h","server_port":1234,"unknown_field":true}"""
        val cfg = json.decodeFromString<AppConfig>(text)
        assertEquals("h", cfg.serverHost)
        assertEquals(1234, cfg.serverPort)
    }

    // -- load() resilience -----------------------------------------------------

    @Test
    fun `load returns defaults for missing file`() {
        val cfg = AppConfig.load("/nonexistent/path/config.json")
        assertEquals(AppConfig(), cfg)
    }

    @Test
    fun `load returns defaults for malformed json`(@TempDir dir: File) {
        val file = File(dir, "bad.json").apply { writeText("{not valid json") }
        val cfg = AppConfig.load(file.absolutePath)
        assertEquals(AppConfig(), cfg)
    }

    @Test
    fun `load validates after decoding`(@TempDir dir: File) {
        val file = File(dir, "cfg.json").apply {
            writeText("""{"server_port": -5, "screen_fps": 0}""")
        }
        val cfg = AppConfig.load(file.absolutePath)
        assertEquals(1, cfg.serverPort)
        assertEquals(1, cfg.screenFps)
    }

    // -- save + load round-trip ------------------------------------------------

    @Test
    fun `save then load round-trip`(@TempDir dir: File) {
        val original = AppConfig(
            serverHost = "remote",
            serverPort = 5555,
            screenFps = 144,
            noiseGateThreshold = 0.1f,
            micDeviceId = "mic-1",
            outputDeviceId = "out-2",
            turnServers = listOf(TurnServerConfig("turn:t", "u", "p")),
        )
        val path = File(dir, "sub/config.json").absolutePath
        AppConfig.save(original, path)
        val loaded = AppConfig.load(path)
        assertEquals(original, loaded)
    }
}
