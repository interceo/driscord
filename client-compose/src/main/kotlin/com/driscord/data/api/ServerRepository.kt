package com.driscord.data.api

import com.driscord.domain.model.Channel
import com.driscord.domain.model.ChannelKind
import com.driscord.domain.model.Server

interface ServerRepository {
    suspend fun listServers(): Result<List<Server>>
    suspend fun createServer(name: String): Result<Server>
    suspend fun joinServer(serverId: Int): Result<Unit>
    suspend fun leaveServer(serverId: Int): Result<Unit>
    suspend fun listChannels(serverId: Int): Result<List<Channel>>
    suspend fun createChannel(serverId: Int, name: String, kind: ChannelKind): Result<Channel>
}
