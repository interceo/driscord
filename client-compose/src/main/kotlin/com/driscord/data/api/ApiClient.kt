package com.driscord.data.api

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.SerializationStrategy
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration

class ApiException(message: String, val code: Int) : Exception(message)

class ApiClient(private val baseUrl: String) {

    private val http: HttpClient = HttpClient.newBuilder()
        .version(HttpClient.Version.HTTP_1_1)
        .connectTimeout(Duration.ofSeconds(10))
        .build()

    val json: Json = Json {
        ignoreUnknownKeys = true
        coerceInputValues = true
    }

    var accessToken: String? = null

    init {
        log("initialized with baseUrl=$baseUrl")
    }

    // ------------------------------------------------------------------
    // Public HTTP helpers
    // ------------------------------------------------------------------

    suspend fun <T> get(path: String, deserializer: DeserializationStrategy<T>): Result<T> =
        withContext(Dispatchers.IO) {
            runCatching {
                val req = requestBuilder(path).GET().build()
                log("GET ${req.uri()}")
                val resp = http.send(req, HttpResponse.BodyHandlers.ofString())
                log("GET ${req.uri()} → ${resp.statusCode()}")
                checkStatus(resp)
                json.decodeFromString(deserializer, resp.body())
            }.onFailure { logError("GET $path", it) }
        }

    suspend fun <B, T> post(
        path: String,
        body: B,
        bodySerializer: SerializationStrategy<B>,
        responseDeserializer: DeserializationStrategy<T>,
    ): Result<T> = withContext(Dispatchers.IO) {
        runCatching {
            val bodyStr = json.encodeToString(bodySerializer, body)
            val req = requestBuilder(path)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(bodyStr))
                .build()
            log("POST ${req.uri()}")
            val resp = http.send(req, HttpResponse.BodyHandlers.ofString())
            log("POST ${req.uri()} → ${resp.statusCode()}")
            checkStatus(resp)
            json.decodeFromString(responseDeserializer, resp.body())
        }.onFailure { logError("POST $path", it) }
    }

    /** POST with no request body; ignores response body (handles 201 and 204). */
    suspend fun postVoid(path: String): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val req = requestBuilder(path)
                .POST(HttpRequest.BodyPublishers.noBody())
                .build()
            log("POST ${req.uri()}")
            val resp = http.send(req, HttpResponse.BodyHandlers.ofString())
            log("POST ${req.uri()} → ${resp.statusCode()}")
            checkStatus(resp)
        }.onFailure { logError("POST $path", it) }
    }

    /** POST with no request body; deserializes JSON response. */
    suspend fun <T> postEmpty(
        path: String,
        responseDeserializer: DeserializationStrategy<T>,
    ): Result<T> = withContext(Dispatchers.IO) {
        runCatching {
            val req = requestBuilder(path)
                .POST(HttpRequest.BodyPublishers.noBody())
                .build()
            log("POST ${req.uri()}")
            val resp = http.send(req, HttpResponse.BodyHandlers.ofString())
            log("POST ${req.uri()} → ${resp.statusCode()}")
            checkStatus(resp)
            json.decodeFromString(responseDeserializer, resp.body())
        }.onFailure { logError("POST $path", it) }
    }

    suspend fun delete(path: String): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val req = requestBuilder(path).DELETE().build()
            log("DELETE ${req.uri()}")
            val resp = http.send(req, HttpResponse.BodyHandlers.ofString())
            log("DELETE ${req.uri()} → ${resp.statusCode()}")
            checkStatus(resp)
        }.onFailure { logError("DELETE $path", it) }
    }

    // ------------------------------------------------------------------
    // Internal helpers
    // ------------------------------------------------------------------

    private fun requestBuilder(path: String): HttpRequest.Builder {
        val builder = HttpRequest.newBuilder()
            .uri(URI.create("$baseUrl$path"))
            .timeout(Duration.ofSeconds(15))
        accessToken?.let { builder.header("Authorization", "Bearer $it") }
        return builder
    }

    private fun checkStatus(resp: HttpResponse<String>) {
        if (resp.statusCode() in 200..299) return
        val detail = runCatching {
            val obj = json.parseToJsonElement(resp.body()) as? JsonObject
            (obj?.get("detail") as? JsonPrimitive)?.content
        }.getOrNull() ?: resp.body().takeIf { it.isNotBlank() } ?: "HTTP ${resp.statusCode()}"
        throw ApiException(detail, resp.statusCode())
    }

    private fun log(msg: String) = println("[api] $msg")
    private fun logError(op: String, t: Throwable) =
        println("[api] $op failed: ${t::class.simpleName}: ${t.message}")
}
