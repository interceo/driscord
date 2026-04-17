package com.driscord.data.api

import com.driscord.domain.model.ChannelKind
import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.test.runTest
import java.net.InetSocketAddress
import kotlin.test.*

class ServerRepositoryImplTest {

    private fun mockServer(path: String, status: Int, body: String): Pair<HttpServer, String> {
        val server = HttpServer.create(InetSocketAddress(0), 0)
        server.createContext(path) { exchange ->
            val bytes = body.toByteArray()
            exchange.sendResponseHeaders(status, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
        return server to "http://localhost:${server.address.port}"
    }

    // ---------------------------------------------------------------------------
    // listServers
    // ---------------------------------------------------------------------------

    @Test
    fun `listServers maps response to domain models`() = runTest {
        val json = """[
            {"id":1,"name":"Alpha","description":"desc","owner_id":10,"created_at":"2024-01-01T00:00:00Z"},
            {"id":2,"name":"Beta","description":null,"owner_id":11,"created_at":"2024-01-01T00:00:00Z"}
        ]"""
        val (server, url) = mockServer("/servers/", 200, json)
        try {
            val repo = ServerRepositoryImpl(ApiClient(url))
            val result = repo.listServers()
            val servers = result.getOrThrow()
            assertEquals(2, servers.size)
            assertEquals(1, servers[0].id)
            assertEquals("Alpha", servers[0].name)
            assertEquals("desc", servers[0].description)
            assertEquals(10, servers[0].ownerId)
            assertNull(servers[1].description)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `listServers returns empty list for empty array`() = runTest {
        val (server, url) = mockServer("/servers/", 200, "[]")
        try {
            val result = ServerRepositoryImpl(ApiClient(url)).listServers()
            assertTrue(result.getOrThrow().isEmpty())
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `listServers returns failure on 401`() = runTest {
        val (server, url) = mockServer("/servers/", 401, """{"detail":"Unauthorized"}""")
        try {
            val result = ServerRepositoryImpl(ApiClient(url)).listServers()
            assertTrue(result.isFailure)
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // createServer
    // ---------------------------------------------------------------------------

    @Test
    fun `createServer returns created server domain model`() = runTest {
        val responseJson = """{"id":5,"name":"My Guild","description":null,"owner_id":1,"created_at":"2024-01-01T00:00:00Z"}"""
        var receivedBody = ""
        val httpServer = HttpServer.create(InetSocketAddress(0), 0)
        httpServer.createContext("/servers/") { exchange ->
            receivedBody = exchange.requestBody.bufferedReader().readText()
            val bytes = responseJson.toByteArray()
            exchange.sendResponseHeaders(201, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        httpServer.start()
        val url = "http://localhost:${httpServer.address.port}"
        try {
            val result = ServerRepositoryImpl(ApiClient(url)).createServer("My Guild")
            val s = result.getOrThrow()
            assertEquals(5, s.id)
            assertEquals("My Guild", s.name)
            assertContains(receivedBody, "\"name\":\"My Guild\"")
        } finally {
            httpServer.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // listChannels
    // ---------------------------------------------------------------------------

    @Test
    fun `listChannels maps voice and text channels correctly`() = runTest {
        val json = """[
            {"id":10,"server_id":1,"name":"general","kind":"voice","position":0,"created_at":"2024-01-01T00:00:00Z"},
            {"id":11,"server_id":1,"name":"announcements","kind":"text","position":1,"created_at":"2024-01-01T00:00:00Z"}
        ]"""
        val (server, url) = mockServer("/servers/1/channels/", 200, json)
        try {
            val repo = ServerRepositoryImpl(ApiClient(url))
            val channels = repo.listChannels(1).getOrThrow()
            assertEquals(2, channels.size)
            assertEquals(ChannelKind.voice, channels[0].kind)
            assertEquals("general", channels[0].name)
            assertEquals(ChannelKind.text, channels[1].kind)
            assertEquals("announcements", channels[1].name)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `listChannels calls correct server-scoped url`() = runTest {
        var calledPath = ""
        val httpServer = HttpServer.create(InetSocketAddress(0), 0)
        httpServer.createContext("/") { exchange ->
            calledPath = exchange.requestURI.path
            val bytes = "[]".toByteArray()
            exchange.sendResponseHeaders(200, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        httpServer.start()
        val url = "http://localhost:${httpServer.address.port}"
        try {
            ServerRepositoryImpl(ApiClient(url)).listChannels(42)
            assertEquals("/servers/42/channels/", calledPath)
        } finally {
            httpServer.stop(0)
        }
    }

    @Test
    fun `listChannels unknown kind defaults to text`() = runTest {
        val json = """[{"id":1,"server_id":1,"name":"weird","kind":"unknown","position":0,"created_at":"2024-01-01T00:00:00Z"}]"""
        val (server, url) = mockServer("/servers/1/channels/", 200, json)
        try {
            val channel = ServerRepositoryImpl(ApiClient(url)).listChannels(1).getOrThrow().first()
            assertEquals(ChannelKind.text, channel.kind)
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // joinServer / leaveServer
    // ---------------------------------------------------------------------------

    @Test
    fun `joinServer returns success on 201`() = runTest {
        val (server, url) = mockServer("/servers/3/members", 201, """{"status":"joined"}""")
        try {
            assertTrue(ServerRepositoryImpl(ApiClient(url)).joinServer(3).isSuccess)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `leaveServer returns success on 204`() = runTest {
        val (server, url) = mockServer("/servers/3/members", 204, "")
        try {
            assertTrue(ServerRepositoryImpl(ApiClient(url)).leaveServer(3).isSuccess)
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // createChannel
    // ---------------------------------------------------------------------------

    @Test
    fun `createChannel posts correct kind and returns domain model`() = runTest {
        var receivedBody = ""
        val responseJson = """{"id":20,"server_id":1,"name":"lobby","kind":"voice","position":0,"created_at":"2024-01-01T00:00:00Z"}"""
        val httpServer = HttpServer.create(InetSocketAddress(0), 0)
        httpServer.createContext("/servers/1/channels/") { exchange ->
            receivedBody = exchange.requestBody.bufferedReader().readText()
            val bytes = responseJson.toByteArray()
            exchange.sendResponseHeaders(201, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        httpServer.start()
        val url = "http://localhost:${httpServer.address.port}"
        try {
            val result = ServerRepositoryImpl(ApiClient(url)).createChannel(1, "lobby", ChannelKind.voice)
            val ch = result.getOrThrow()
            assertEquals(20, ch.id)
            assertEquals("lobby", ch.name)
            assertEquals(ChannelKind.voice, ch.kind)
            assertContains(receivedBody, "\"kind\":\"voice\"")
            assertContains(receivedBody, "\"name\":\"lobby\"")
        } finally {
            httpServer.stop(0)
        }
    }

    @Test
    fun `createChannel text kind sends correct kind string`() = runTest {
        var receivedBody = ""
        val responseJson = """{"id":21,"server_id":1,"name":"news","kind":"text","position":0,"created_at":"2024-01-01T00:00:00Z"}"""
        val httpServer = HttpServer.create(InetSocketAddress(0), 0)
        httpServer.createContext("/") { exchange ->
            receivedBody = exchange.requestBody.bufferedReader().readText()
            val bytes = responseJson.toByteArray()
            exchange.sendResponseHeaders(201, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        httpServer.start()
        val url = "http://localhost:${httpServer.address.port}"
        try {
            ServerRepositoryImpl(ApiClient(url)).createChannel(1, "news", ChannelKind.text)
            assertContains(receivedBody, "\"kind\":\"text\"")
        } finally {
            httpServer.stop(0)
        }
    }
}
