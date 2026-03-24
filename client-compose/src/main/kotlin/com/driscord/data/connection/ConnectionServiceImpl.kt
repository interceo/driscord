package com.driscord.data.connection

import com.driscord.AppConfig
import com.driscord.domain.model.ConnectionState
import com.driscord.domain.model.PeerInfo
import com.driscord.jni.NativeTransport
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json

class ConnectionServiceImpl(config: AppConfig) : ConnectionService {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val json = Json { ignoreUnknownKeys = true }

    private val _connectionState = MutableStateFlow(ConnectionState.Disconnected)
    override val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _localId = MutableStateFlow("")
    override val localId: StateFlow<String> = _localId.asStateFlow()

    private val _peers = MutableStateFlow<List<PeerInfo>>(emptyList())
    override val peers: StateFlow<List<PeerInfo>> = _peers.asStateFlow()

    private val _peerJoinedEvents = MutableSharedFlow<String>(extraBufferCapacity = 16)
    override val peerJoinedEvents: SharedFlow<String> = _peerJoinedEvents.asSharedFlow()

    private val _peerLeftEvents = MutableSharedFlow<String>(extraBufferCapacity = 16)
    override val peerLeftEvents: SharedFlow<String> = _peerLeftEvents.asSharedFlow()

    init {
        config.turnServers.forEach { ts ->
            NativeTransport.addTurnServer(ts.url, ts.user, ts.pass)
        }

        NativeTransport.setOnPeerJoined { peerId ->
            scope.launch {
                _peerJoinedEvents.emit(peerId)
                withContext(Dispatchers.Main) { refreshPeers() }
            }
        }
        NativeTransport.setOnPeerLeft { peerId ->
            scope.launch {
                _peerLeftEvents.emit(peerId)
                withContext(Dispatchers.Main) { refreshPeers() }
            }
        }
    }

    override fun connect(serverUrl: String) {
        if (_connectionState.value != ConnectionState.Disconnected) return
        _connectionState.value = ConnectionState.Connecting
        NativeTransport.connect(serverUrl)
        scope.launch {
            while (isActive && !NativeTransport.connected()) delay(100)
            if (NativeTransport.connected()) {
                withContext(Dispatchers.Main) {
                    _localId.value = NativeTransport.localId()
                    _connectionState.value = ConnectionState.Connected
                }
            }
        }
    }

    override fun disconnect() {
        NativeTransport.disconnect()
        _connectionState.value = ConnectionState.Disconnected
        _peers.value = emptyList()
        _localId.value = ""
    }

    override fun destroy() {
        scope.cancel()
    }

    private fun refreshPeers() {
        _peers.value = json.decodeFromString(NativeTransport.peers())
    }
}
