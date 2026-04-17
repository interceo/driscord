package com.driscord.data

import com.driscord.data.api.ServerRepository
import com.driscord.domain.model.Channel
import com.driscord.domain.model.ChannelKind
import com.driscord.domain.model.Server

class FakeServerRepository : ServerRepository {

    var listServersResult: Result<List<Server>> = Result.success(emptyList())
    var createServerResult: Result<Server> = Result.success(Server(1, "New Server", null, 1))
    var joinServerResult: Result<Unit> = Result.success(Unit)
    var leaveServerResult: Result<Unit> = Result.success(Unit)
    var listChannelsResult: Result<List<Channel>> = Result.success(emptyList())
    var createChannelResult: Result<Channel> = Result.success(Channel(1, 1, "New Channel", ChannelKind.voice, 0))

    var listServersCalled = 0
    var createServerCalls = mutableListOf<String>()
    var joinServerCalls = mutableListOf<Int>()
    var leaveServerCalls = mutableListOf<Int>()
    var listChannelsCalls = mutableListOf<Int>()
    var createChannelCalls = mutableListOf<Triple<Int, String, ChannelKind>>()

    override suspend fun listServers(): Result<List<Server>> {
        listServersCalled++
        return listServersResult
    }

    override suspend fun createServer(name: String): Result<Server> {
        createServerCalls += name
        return createServerResult
    }

    override suspend fun joinServer(serverId: Int): Result<Unit> {
        joinServerCalls += serverId
        return joinServerResult
    }

    override suspend fun leaveServer(serverId: Int): Result<Unit> {
        leaveServerCalls += serverId
        return leaveServerResult
    }

    override suspend fun listChannels(serverId: Int): Result<List<Channel>> {
        listChannelsCalls += serverId
        return listChannelsResult
    }

    override suspend fun createChannel(serverId: Int, name: String, kind: ChannelKind): Result<Channel> {
        createChannelCalls += Triple(serverId, name, kind)
        return createChannelResult
    }
}
