package com.driscord.data.api

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.Serializable
import kotlinx.serialization.builtins.ListSerializer
import java.net.InetSocketAddress
import kotlin.test.*

/**
 * Tests for ApiClient using a local JDK HttpServer — no external dependencies.
 *
 * Each test gets a fresh server on a random ephemeral port, so tests can run in parallel.
 */
class ApiClientTest {

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    @Serializable
    private data class Payload(val value: String)

    /** Starts a JDK HttpServer that responds to one request with [status] and [body]. */
    private fun mockServer(path: String, status: Int, body: String): Pair<HttpServer, String> {
        val server = HttpServer.create(InetSocketAddress(0), 0)
        server.createContext(path) { exchange ->
            val bytes = body.toByteArray()
            exchange.sendResponseHeaders(status, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
        val url = "http://localhost:${server.address.port}"
        return server to url
    }

    // ---------------------------------------------------------------------------
    // GET
    // ---------------------------------------------------------------------------

    @Test
    fun `get deserializes response body`() = runTest {
        val (server, url) = mockServer("/items", 200, """{"value":"hello"}""")
        try {
            val client = ApiClient(url)
            val result = client.get("/items", Payload.serializer())
            assertEquals(Payload("hello"), result.getOrThrow())
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `get returns failure on 404`() = runTest {
        val (server, url) = mockServer("/missing", 404, """{"detail":"not found"}""")
        try {
            val client = ApiClient(url)
            val result = client.get("/missing", Payload.serializer())
            assertTrue(result.isFailure)
            assertContains(result.exceptionOrNull()!!.message!!, "not found")
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `get returns failure on 500`() = runTest {
        val (server, url) = mockServer("/error", 500, "Internal Server Error")
        try {
            val client = ApiClient(url)
            val result = client.get("/error", Payload.serializer())
            assertTrue(result.isFailure)
            val ex = result.exceptionOrNull() as ApiException
            assertEquals(500, ex.code)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `get sends Authorization header when token is set`() = runTest {
        var receivedAuth: String? = null
        val server = HttpServer.create(InetSocketAddress(0), 0)
        server.createContext("/secure") { exchange ->
            receivedAuth = exchange.requestHeaders.getFirst("Authorization")
            val bytes = """{"value":"ok"}""".toByteArray()
            exchange.sendResponseHeaders(200, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
        val url = "http://localhost:${server.address.port}"
        try {
            val client = ApiClient(url).also { it.accessToken = "my-token" }
            client.get("/secure", Payload.serializer())
            assertEquals("Bearer my-token", receivedAuth)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `get deserializes list response`() = runTest {
        val body = """[{"value":"a"},{"value":"b"}]"""
        val (server, url) = mockServer("/list", 200, body)
        try {
            val client = ApiClient(url)
            val result = client.get("/list", ListSerializer(Payload.serializer()))
            assertEquals(listOf(Payload("a"), Payload("b")), result.getOrThrow())
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // POST
    // ---------------------------------------------------------------------------

    @Test
    fun `post sends json body and deserializes response`() = runTest {
        var receivedBody: String? = null
        val server = HttpServer.create(InetSocketAddress(0), 0)
        server.createContext("/echo") { exchange ->
            receivedBody = exchange.requestBody.bufferedReader().readText()
            val resp = """{"value":"echoed"}""".toByteArray()
            exchange.sendResponseHeaders(200, resp.size.toLong())
            exchange.responseBody.use { it.write(resp) }
        }
        server.start()
        val url = "http://localhost:${server.address.port}"
        try {
            val client = ApiClient(url)
            val result = client.post(
                "/echo",
                Payload("test"),
                Payload.serializer(),
                Payload.serializer(),
            )
            assertEquals(Payload("echoed"), result.getOrThrow())
            assertContains(receivedBody!!, "\"value\":\"test\"")
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `post sends Content-Type application json header`() = runTest {
        var contentType: String? = null
        val server = HttpServer.create(InetSocketAddress(0), 0)
        server.createContext("/check") { exchange ->
            contentType = exchange.requestHeaders.getFirst("Content-Type")
            val resp = """{"value":"ok"}""".toByteArray()
            exchange.sendResponseHeaders(200, resp.size.toLong())
            exchange.responseBody.use { it.write(resp) }
        }
        server.start()
        val url = "http://localhost:${server.address.port}"
        try {
            val client = ApiClient(url)
            client.post("/check", Payload("x"), Payload.serializer(), Payload.serializer())
            assertEquals("application/json", contentType)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `post returns failure on 409`() = runTest {
        val (server, url) = mockServer("/conflict", 409, """{"detail":"already exists"}""")
        try {
            val client = ApiClient(url)
            val result = client.post("/conflict", Payload("x"), Payload.serializer(), Payload.serializer())
            assertTrue(result.isFailure)
            val ex = result.exceptionOrNull() as ApiException
            assertEquals(409, ex.code)
            assertContains(ex.message!!, "already exists")
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // postVoid
    // ---------------------------------------------------------------------------

    @Test
    fun `postVoid returns success on 201`() = runTest {
        val (server, url) = mockServer("/join", 201, """{"status":"joined"}""")
        try {
            val client = ApiClient(url)
            val result = client.postVoid("/join")
            assertTrue(result.isSuccess)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `postVoid returns success on 204`() = runTest {
        val (server, url) = mockServer("/leave", 204, "")
        try {
            val client = ApiClient(url)
            val result = client.postVoid("/leave")
            assertTrue(result.isSuccess)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `postVoid returns failure on 401`() = runTest {
        val (server, url) = mockServer("/auth", 401, """{"detail":"Unauthorized"}""")
        try {
            val client = ApiClient(url)
            val result = client.postVoid("/auth")
            assertTrue(result.isFailure)
            val ex = result.exceptionOrNull() as ApiException
            assertEquals(401, ex.code)
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // delete
    // ---------------------------------------------------------------------------

    @Test
    fun `delete returns success on 204`() = runTest {
        val (server, url) = mockServer("/resource", 204, "")
        try {
            val client = ApiClient(url)
            val result = client.delete("/resource")
            assertTrue(result.isSuccess)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `delete returns failure on 403`() = runTest {
        val (server, url) = mockServer("/resource", 403, """{"detail":"Forbidden"}""")
        try {
            val client = ApiClient(url)
            val result = client.delete("/resource")
            assertTrue(result.isFailure)
            val ex = result.exceptionOrNull() as ApiException
            assertEquals(403, ex.code)
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // Error message extraction
    // ---------------------------------------------------------------------------

    @Test
    fun `error detail is extracted from json detail field`() = runTest {
        val (server, url) = mockServer("/fail", 400, """{"detail":"Bad request body"}""")
        try {
            val client = ApiClient(url)
            val result = client.get("/fail", Payload.serializer())
            assertEquals("Bad request body", result.exceptionOrNull()!!.message)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `error falls back to raw body when no detail field`() = runTest {
        val (server, url) = mockServer("/fail", 400, "plain text error")
        try {
            val client = ApiClient(url)
            val result = client.get("/fail", Payload.serializer())
            assertEquals("plain text error", result.exceptionOrNull()!!.message)
        } finally {
            server.stop(0)
        }
    }
}
