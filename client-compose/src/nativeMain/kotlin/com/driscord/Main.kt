package com.driscord

import kotlinx.cinterop.ExperimentalForeignApi

/**
 * Kotlin/Native entry point — minimal CLI client.
 *
 * Usage:
 *   driscord [server-url]
 *   driscord ws://localhost:9001
 */
@OptIn(ExperimentalForeignApi::class)
fun main(args: Array<String>) {
    println("Driscord native client (Kotlin/Native linuxX64)")

    val serverUrl = args.firstOrNull() ?: run {
        println("Usage: driscord <server-url>  (e.g. ws://localhost:9001)")
        return
    }

    println("Connecting to $serverUrl ...")
    NativeDriscord.connect(serverUrl)

    // Poll briefly to let the connection establish
    var attempts = 0
    while (!NativeDriscord.connected() && attempts++ < 50) {
        kotlinx.cinterop.memScoped {
            platform.posix.usleep(100_000u) // 100 ms
        }
    }

    if (NativeDriscord.connected()) {
        val id = NativeDriscord.localId()
        println("Connected. Local ID: $id")
        println("Peers: ${NativeDriscord.peers()}")

        NativeDriscord.audioStart()
        println("Audio started. Press Enter to disconnect.")
        readLine()

        NativeDriscord.audioStop()
        NativeDriscord.disconnect()
    } else {
        println("Failed to connect to $serverUrl")
    }
}
