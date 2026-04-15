package com.driscord.data.api

import com.driscord.domain.model.Channel
import com.driscord.domain.model.ChannelKind
import com.driscord.domain.model.Server
import kotlinx.serialization.builtins.ListSerializer

class ServerRepositoryImpl(private val client: ApiClient) : ServerRepository {

    override suspend fun listServers(): Result<List<Server>> =
        client.get("/servers/", ListSerializer(ServerResponse.serializer()))
            .map { list -> list.map { it.toDomain() } }

    override suspend fun createServer(name: String): Result<Server> =
        client.post(
            "/servers/",
            CreateServerRequest(name),
            CreateServerRequest.serializer(),
            ServerResponse.serializer(),
        ).map { it.toDomain() }

    override suspend fun joinServer(serverId: Int): Result<Unit> =
        client.postVoid("/servers/$serverId/members")

    override suspend fun leaveServer(serverId: Int): Result<Unit> =
        client.delete("/servers/$serverId/members")

    override suspend fun listChannels(serverId: Int): Result<List<Channel>> =
        client.get("/servers/$serverId/channels/", ListSerializer(ChannelResponse.serializer()))
            .map { list -> list.map { it.toDomain() } }

    override suspend fun createChannel(serverId: Int, name: String, kind: ChannelKind): Result<Channel> =
        client.post(
            "/servers/$serverId/channels/",
            CreateChannelRequest(name, kind.name),
            CreateChannelRequest.serializer(),
            ChannelResponse.serializer(),
        ).map { it.toDomain() }
}

private fun ServerResponse.toDomain() = Server(id, name, description, ownerId)

private fun ChannelResponse.toDomain() = Channel(
    id = id,
    serverId = serverId,
    name = name,
    kind = if (kind == "voice") ChannelKind.voice else ChannelKind.text,
    position = position,
)
