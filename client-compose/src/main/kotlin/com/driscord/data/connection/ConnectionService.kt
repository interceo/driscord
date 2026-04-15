package com.driscord.data.connection

import com.driscord.domain.model.ConnectionState
import com.driscord.domain.model.PeerInfo
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

interface ConnectionService {
    val connectionState: StateFlow<ConnectionState>
    val localId: StateFlow<String>
    val peers: StateFlow<List<PeerInfo>>
    val peerJoinedEvents: SharedFlow<String>
    val peerLeftEvents: SharedFlow<String>
    fun connect(serverUrl: String)
    fun disconnect()
    fun destroy()
    fun setLocalUsername(username: String)
}