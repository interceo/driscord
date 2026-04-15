package com.driscord.data

import com.driscord.data.connection.ConnectionService
import com.driscord.domain.model.ConnectionState
import com.driscord.domain.model.PeerInfo
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

class FakeConnectionService : ConnectionService {

    override val connectionState = MutableStateFlow(ConnectionState.Disconnected)
    override val localId = MutableStateFlow("")
    override val peers = MutableStateFlow<List<PeerInfo>>(emptyList())

    val _peerJoinedEvents = MutableSharedFlow<String>(extraBufferCapacity = 16)
    override val peerJoinedEvents: SharedFlow<String> = _peerJoinedEvents

    val _peerLeftEvents = MutableSharedFlow<String>(extraBufferCapacity = 16)
    override val peerLeftEvents: SharedFlow<String> = _peerLeftEvents

    var connectCalls = mutableListOf<String>()
    var disconnectCalled = 0
    var destroyCalled = 0
    var setLocalUsernameCalls = mutableListOf<String>()

    override fun connect(serverUrl: String) {
        connectCalls += serverUrl
    }

    override fun disconnect() {
        disconnectCalled++
    }

    override fun destroy() {
        destroyCalled++
    }

    override fun setLocalUsername(username: String) {
        setLocalUsernameCalls += username
    }
}
