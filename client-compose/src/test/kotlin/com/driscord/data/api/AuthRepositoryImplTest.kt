package com.driscord.data.api

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.test.runTest
import java.net.InetSocketAddress
import kotlin.test.*

class AuthRepositoryImplTest {

    private fun mockServer(
        path: String,
        status: Int,
        responseBody: String,
        captureBody: ((String) -> Unit)? = null,
    ): Pair<HttpServer, String> {
        val server = HttpServer.create(InetSocketAddress(0), 0)
        server.createContext(path) { exchange ->
            captureBody?.invoke(exchange.requestBody.bufferedReader().readText())
            val bytes = responseBody.toByteArray()
            exchange.sendResponseHeaders(status, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
        return server to "http://localhost:${server.address.port}"
    }

    private val tokenJson = """{"access_token":"acc123","refresh_token":"ref456"}"""

    // ---------------------------------------------------------------------------
    // login
    // ---------------------------------------------------------------------------

    @Test
    fun `login success sets access token on client`() = runTest {
        val (server, url) = mockServer("/auth/login", 200, tokenJson)
        try {
            val client = ApiClient(url)
            val repo = AuthRepositoryImpl(client)
            repo.login("alice", "secret")
            assertEquals("acc123", client.accessToken)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `login success marks repository as logged in`() = runTest {
        val (server, url) = mockServer("/auth/login", 200, tokenJson)
        try {
            val repo = AuthRepositoryImpl(ApiClient(url))
            assertFalse(repo.isLoggedIn)
            repo.login("alice", "secret")
            assertTrue(repo.isLoggedIn)
            assertEquals("alice", repo.currentUsername)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `login failure returns failure result`() = runTest {
        val (server, url) = mockServer("/auth/login", 401, """{"detail":"Invalid credentials"}""")
        try {
            val repo = AuthRepositoryImpl(ApiClient(url))
            val result = repo.login("alice", "wrong")
            assertTrue(result.isFailure)
            assertContains(result.exceptionOrNull()!!.message!!, "Invalid credentials")
            assertFalse(repo.isLoggedIn)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `login sends correct json body`() = runTest {
        var body = ""
        val (server, url) = mockServer("/auth/login", 200, tokenJson) { body = it }
        try {
            AuthRepositoryImpl(ApiClient(url)).login("bob", "pass123")
            assertContains(body, "\"username\":\"bob\"")
            assertContains(body, "\"password\":\"pass123\"")
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // register
    // ---------------------------------------------------------------------------

    @Test
    fun `register success marks repository as logged in`() = runTest {
        val (server, url) = mockServer("/auth/register", 201, tokenJson)
        try {
            val repo = AuthRepositoryImpl(ApiClient(url))
            val result = repo.register("bob", "bob@test.com", "pass")
            assertTrue(result.isSuccess)
            assertTrue(repo.isLoggedIn)
            assertEquals("bob", repo.currentUsername)
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `register conflict returns failure`() = runTest {
        val (server, url) = mockServer("/auth/register", 409, """{"detail":"Username already taken"}""")
        try {
            val repo = AuthRepositoryImpl(ApiClient(url))
            val result = repo.register("alice", "a@b.com", "pass")
            assertTrue(result.isFailure)
            assertContains(result.exceptionOrNull()!!.message!!, "Username already taken")
        } finally {
            server.stop(0)
        }
    }

    @Test
    fun `register sends correct json body`() = runTest {
        var body = ""
        val (server, url) = mockServer("/auth/register", 201, tokenJson) { body = it }
        try {
            AuthRepositoryImpl(ApiClient(url)).register("carol", "carol@test.com", "mypass")
            assertContains(body, "\"username\":\"carol\"")
            assertContains(body, "\"email\":\"carol@test.com\"")
            assertContains(body, "\"password\":\"mypass\"")
        } finally {
            server.stop(0)
        }
    }

    // ---------------------------------------------------------------------------
    // logout
    // ---------------------------------------------------------------------------

    @Test
    fun `logout clears access token and login state`() = runTest {
        val (server, url) = mockServer("/auth/login", 200, tokenJson)
        try {
            val client = ApiClient(url)
            val repo = AuthRepositoryImpl(client)
            repo.login("alice", "secret")
            assertTrue(repo.isLoggedIn)

            repo.logout()
            assertFalse(repo.isLoggedIn)
            assertNull(repo.currentUsername)
            assertNull(client.accessToken)
        } finally {
            server.stop(0)
        }
    }
}
