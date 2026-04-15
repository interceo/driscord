package com.driscord.presentation.viewmodel

import app.cash.turbine.turbineScope
import com.driscord.AppConfig
import com.driscord.data.FakeAudioService
import com.driscord.data.FakeAuthRepository
import com.driscord.data.FakeConfigRepository
import com.driscord.data.FakeConnectionService
import com.driscord.data.FakeServerRepository
import com.driscord.data.FakeVideoService
import com.driscord.domain.model.CaptureTarget
import com.driscord.domain.model.Channel
import com.driscord.domain.model.ChannelKind
import com.driscord.domain.model.Server
import com.driscord.presentation.AuthStatus
import com.driscord.domain.model.ConnectionState
import com.driscord.domain.model.PeerInfo
import com.driscord.presentation.AppIntent
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import kotlin.test.*

@OptIn(ExperimentalCoroutinesApi::class)
class AppViewModelTest {

    private lateinit var connectionService: FakeConnectionService
    private lateinit var audioService: FakeAudioService
    private lateinit var videoService: FakeVideoService
    private lateinit var configRepository: FakeConfigRepository
    private lateinit var authRepository: FakeAuthRepository
    private lateinit var serverRepository: FakeServerRepository
    private lateinit var vm: AppViewModel

    private fun setup(config: AppConfig = AppConfig()): TestScope {
        val testScope = TestScope(UnconfinedTestDispatcher())
        connectionService = FakeConnectionService()
        audioService = FakeAudioService()
        videoService = FakeVideoService()
        configRepository = FakeConfigRepository(config)
        authRepository = FakeAuthRepository()
        serverRepository = FakeServerRepository()
        vm = AppViewModel(
            connectionService, audioService, videoService, configRepository,
            authRepository, serverRepository,
            scope = testScope.backgroundScope,
        )
        return testScope
    }

    // -- Connect intent -------------------------------------------------------

    @Test
    fun `connect calls connectionService with url`() = runTest {
        setup()
        vm.onIntent(AppIntent.Connect("ws://localhost:8080"))
        assertEquals(listOf("ws://localhost:8080"), connectionService.connectCalls)
    }

    @Test
    fun `connect starts audio with config settings`() = runTest {
        val cfg = AppConfig(micDeviceId = "mic-1", outputDeviceId = "out-2", noiseGateThreshold = 0.05f)
        setup(cfg)
        vm.onIntent(AppIntent.Connect(cfg.serverUrl))

        assertEquals(1, audioService.startCalled)
        assertEquals("mic-1", audioService.lastInputDevice)
        assertEquals("out-2", audioService.lastOutputDevice)
        assertEquals(0.05f, audioService.lastNoiseGate)
    }

    // -- Disconnect intent ----------------------------------------------------

    @Test
    fun `disconnect calls all services`() = runTest {
        setup()
        vm.onIntent(AppIntent.Disconnect)

        assertEquals(1, connectionService.disconnectCalled)
        assertEquals(1, audioService.stopCalled)
        assertEquals(1, videoService.stopSharingCalled)
    }

    // -- ToggleMute / ToggleDeafen -------------------------------------------

    @Test
    fun `toggle mute flips muted state`() = runTest {
        setup()
        assertFalse(vm.state.value.muted)
        vm.onIntent(AppIntent.ToggleMute)
        assertEquals(1, audioService.toggleMuteCalled)
        assertTrue(vm.state.value.muted)
    }

    @Test
    fun `toggle deafen sets both deafened and muted`() = runTest {
        setup()
        vm.onIntent(AppIntent.ToggleDeafen)
        assertEquals(1, audioService.toggleDeafenCalled)
        assertTrue(vm.state.value.deafened)
        assertTrue(vm.state.value.muted)
    }

    // -- SetOutputVolume ------------------------------------------------------

    @Test
    fun `set output volume delegates to audio service`() = runTest {
        setup()
        vm.onIntent(AppIntent.SetOutputVolume(0.5f))
        assertEquals(0.5f, audioService.lastOutputVolume)
    }

    // -- SetPeerVolume --------------------------------------------------------

    @Test
    fun `set peer volume delegates to audio service`() = runTest {
        setup()
        vm.onIntent(AppIntent.SetPeerVolume("peer-1", 0.7f))
        assertEquals(0.7f, audioService.peerVolumes["peer-1"])
    }

    // -- State flow merging ---------------------------------------------------

    @Test
    fun `connectionState flow updates ui state`() = runTest {
        setup()
        connectionService.connectionState.value = ConnectionState.Connected
        assertEquals(ConnectionState.Connected, vm.state.value.connectionState)
    }

    @Test
    fun `localId flow updates ui state`() = runTest {
        setup()
        connectionService.localId.value = "my-id"
        assertEquals("my-id", vm.state.value.localId)
    }

    @Test
    fun `peers flow updates ui state`() = runTest {
        setup()
        val list = listOf(PeerInfo("a", true), PeerInfo("b", false))
        connectionService.peers.value = list
        assertEquals(list, vm.state.value.peers)
    }

    @Test
    fun `muted flow updates ui state`() = runTest {
        setup()
        audioService.muted.value = true
        assertTrue(vm.state.value.muted)
    }

    @Test
    fun `streamingPeers flow updates ui state`() = runTest {
        setup()
        videoService.streamingPeers.value = listOf("peer-x")
        assertEquals(listOf("peer-x"), vm.state.value.streamingPeers)
    }

    @Test
    fun `outputVolume flow updates ui state`() = runTest {
        setup()
        audioService.outputVolume.value = 0.3f
        assertEquals(0.3f, vm.state.value.outputVolume)
    }

    // -- Share dialog ---------------------------------------------------------

    @Test
    fun `open share dialog sets flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenShareDialog)
        assertTrue(vm.state.value.showShareDialog)
    }

    @Test
    fun `dismiss share dialog clears flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenShareDialog)
        vm.onIntent(AppIntent.DismissShareDialog)
        assertFalse(vm.state.value.showShareDialog)
    }

    @Test
    fun `start sharing success closes dialog`() = runTest {
        setup()
        videoService.startSharingResult = true
        vm.onIntent(AppIntent.OpenShareDialog)
        vm.onIntent(AppIntent.StartSharing(
            target = CaptureTarget(0, "0", "Monitor", 1920, 1080, 0, 0),
            quality = 2, fps = 30, shareAudio = false,
        ))
        assertFalse(vm.state.value.showShareDialog)
    }

    @Test
    fun `start sharing failure keeps dialog open`() = runTest {
        setup()
        videoService.startSharingResult = false
        vm.onIntent(AppIntent.OpenShareDialog)
        vm.onIntent(AppIntent.StartSharing(
            target = CaptureTarget(0, "0", "Monitor", 1920, 1080, 0, 0),
            quality = 2, fps = 30, shareAudio = false,
        ))
        assertTrue(vm.state.value.showShareDialog)
    }

    // -- StopSharing ----------------------------------------------------------

    @Test
    fun `stop sharing delegates to video service`() = runTest {
        setup()
        vm.onIntent(AppIntent.StopSharing)
        assertEquals(1, videoService.stopSharingCalled)
    }

    // -- JoinStream / LeaveStream ---------------------------------------------

    @Test
    fun `join stream delegates to video service`() = runTest {
        setup()
        vm.onIntent(AppIntent.JoinStream)
        assertEquals(1, videoService.joinStreamCalled)
    }

    @Test
    fun `leave stream delegates to video service`() = runTest {
        setup()
        vm.onIntent(AppIntent.LeaveStream)
        assertEquals(1, videoService.leaveStreamCalled)
    }

    // -- SetStreamVolume ------------------------------------------------------

    @Test
    fun `set stream volume delegates to video service`() = runTest {
        setup()
        vm.onIntent(AppIntent.SetStreamVolume("peer-1", 0.8f))
        assertEquals(0.8f, videoService.streamVolumes["peer-1"])
    }

    // -- Settings dialog ------------------------------------------------------

    @Test
    fun `open settings sets flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenSettings)
        assertTrue(vm.state.value.showSettings)
    }

    @Test
    fun `dismiss settings clears flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenSettings)
        vm.onIntent(AppIntent.DismissSettings)
        assertFalse(vm.state.value.showSettings)
    }

    // -- SaveConfig -----------------------------------------------------------

    @Test
    fun `save config persists and closes settings`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenSettings)
        val newCfg = AppConfig(serverHost = "remote", serverPort = 9090)
        vm.onIntent(AppIntent.SaveConfig(newCfg))

        assertEquals(listOf(newCfg), configRepository.savedConfigs)
        assertFalse(vm.state.value.showSettings)
    }

    @Test
    fun `save config updates input device when changed`() = runTest {
        setup(AppConfig(micDeviceId = "old-mic"))
        val newCfg = AppConfig(micDeviceId = "new-mic")
        vm.onIntent(AppIntent.SaveConfig(newCfg))

        assertEquals("new-mic", audioService.lastInputDevice)
    }

    @Test
    fun `save config does not update input device when unchanged`() = runTest {
        setup(AppConfig(micDeviceId = "same-mic"))
        val newCfg = AppConfig(micDeviceId = "same-mic")
        vm.onIntent(AppIntent.SaveConfig(newCfg))

        assertNull(audioService.lastInputDevice)
    }

    @Test
    fun `save config updates output device when changed`() = runTest {
        setup(AppConfig(outputDeviceId = "old-out"))
        val newCfg = AppConfig(outputDeviceId = "new-out")
        vm.onIntent(AppIntent.SaveConfig(newCfg))

        assertEquals("new-out", audioService.lastOutputDevice)
    }

    @Test
    fun `save config does not update output device when unchanged`() = runTest {
        setup(AppConfig(outputDeviceId = "same-out"))
        val newCfg = AppConfig(outputDeviceId = "same-out")
        vm.onIntent(AppIntent.SaveConfig(newCfg))

        assertNull(audioService.lastOutputDevice)
    }

    // -- Peer lifecycle forwarding --------------------------------------------

    @Test
    fun `peer joined event forwarded to audio service`() = runTest {
        setup()
        connectionService._peerJoinedEvents.emit("peer-a")
        // Let the coroutine collecting peerJoinedEvents run
        assertTrue(audioService.peersJoined.contains("peer-a"))
    }

    @Test
    fun `peer left event forwarded to audio service`() = runTest {
        setup()
        connectionService._peerLeftEvents.emit("peer-b")
        assertTrue(audioService.peersLeft.contains("peer-b"))
    }

    // -- close() --------------------------------------------------------------

    @Test
    fun `close destroys all services`() = runTest {
        setup()
        vm.close()

        assertEquals(1, videoService.destroyCalled)
        assertEquals(1, audioService.destroyCalled)
        assertEquals(1, connectionService.destroyCalled)
    }

    // -- Synchronous getters --------------------------------------------------

    @Test
    fun `getPeerVolume delegates to audio service`() = runTest {
        setup()
        audioService.peerVolumes["p1"] = 0.4f
        assertEquals(0.4f, vm.getPeerVolume("p1"))
    }

    @Test
    fun `getStreamVolume delegates to video service`() = runTest {
        setup()
        assertEquals(1.0f, vm.getStreamVolume())
    }

    // -- Login intent ---------------------------------------------------------

    @Test
    fun `login success sets LoggedIn status and username`() = runTest {
        setup()
        authRepository.loginResult = Result.success(Unit)
        vm.onIntent(AppIntent.Login("alice", "secret"))

        assertEquals(AuthStatus.LoggedIn, vm.state.value.authStatus)
        assertEquals("alice", vm.state.value.currentUsername)
    }

    @Test
    fun `login success broadcasts username to connection service`() = runTest {
        setup()
        authRepository.loginResult = Result.success(Unit)
        vm.onIntent(AppIntent.Login("alice", "secret"))

        assertEquals(listOf("alice"), connectionService.setLocalUsernameCalls)
    }

    @Test
    fun `login success triggers server list refresh`() = runTest {
        setup()
        authRepository.loginResult = Result.success(Unit)
        serverRepository.listServersResult = Result.success(listOf(Server(1, "TestServer", null, 1)))
        vm.onIntent(AppIntent.Login("alice", "secret"))

        assertEquals(1, serverRepository.listServersCalled)
        assertEquals(1, vm.state.value.servers.size)
        assertEquals("TestServer", vm.state.value.servers.first().name)
    }

    @Test
    fun `login failure sets LoggedOut and stores error`() = runTest {
        setup()
        authRepository.loginResult = Result.failure(RuntimeException("Invalid credentials"))
        vm.onIntent(AppIntent.Login("alice", "wrong"))

        assertEquals(AuthStatus.LoggedOut, vm.state.value.authStatus)
        assertEquals("Invalid credentials", vm.state.value.apiError)
    }

    @Test
    fun `login calls auth repository with correct credentials`() = runTest {
        setup()
        vm.onIntent(AppIntent.Login("alice", "secret"))

        assertEquals(listOf(Pair("alice", "secret")), authRepository.loginCalls)
    }

    // -- Register intent ------------------------------------------------------

    @Test
    fun `register success sets LoggedIn status`() = runTest {
        setup()
        authRepository.registerResult = Result.success(Unit)
        vm.onIntent(AppIntent.Register("bob", "bob@example.com", "pass"))

        assertEquals(AuthStatus.LoggedIn, vm.state.value.authStatus)
        assertEquals("bob", vm.state.value.currentUsername)
    }

    @Test
    fun `register success broadcasts username to connection service`() = runTest {
        setup()
        authRepository.registerResult = Result.success(Unit)
        vm.onIntent(AppIntent.Register("bob", "bob@example.com", "pass"))

        assertEquals(listOf("bob"), connectionService.setLocalUsernameCalls)
    }

    @Test
    fun `register failure stores error message`() = runTest {
        setup()
        authRepository.registerResult = Result.failure(RuntimeException("Username already taken"))
        vm.onIntent(AppIntent.Register("alice", "a@b.com", "pass"))

        assertEquals(AuthStatus.LoggedOut, vm.state.value.authStatus)
        assertEquals("Username already taken", vm.state.value.apiError)
    }

    @Test
    fun `register calls auth repository with correct params`() = runTest {
        setup()
        vm.onIntent(AppIntent.Register("bob", "bob@test.com", "pass123"))

        assertEquals(
            listOf(Triple("bob", "bob@test.com", "pass123")),
            authRepository.registerCalls,
        )
    }

    // -- Logout intent --------------------------------------------------------

    @Test
    fun `logout calls auth repository and resets state`() = runTest {
        setup()
        authRepository.loginResult = Result.success(Unit)
        vm.onIntent(AppIntent.Login("alice", "secret"))
        vm.onIntent(AppIntent.Logout)

        assertEquals(1, authRepository.logoutCalled)
        assertEquals(AuthStatus.LoggedOut, vm.state.value.authStatus)
        assertEquals("", vm.state.value.currentUsername)
        assertTrue(vm.state.value.servers.isEmpty())
    }

    @Test
    fun `logout disconnects voice and stops services`() = runTest {
        setup()
        vm.onIntent(AppIntent.Logout)

        assertEquals(1, connectionService.disconnectCalled)
        assertEquals(1, audioService.stopCalled)
        assertEquals(1, videoService.stopSharingCalled)
    }

    // -- DismissApiError intent -----------------------------------------------

    @Test
    fun `dismiss api error clears error message`() = runTest {
        setup()
        authRepository.loginResult = Result.failure(RuntimeException("Oops"))
        vm.onIntent(AppIntent.Login("x", "y"))

        assertNotNull(vm.state.value.apiError)
        vm.onIntent(AppIntent.DismissApiError)
        assertNull(vm.state.value.apiError)
    }

    // -- SelectServer intent --------------------------------------------------

    @Test
    fun `select server loads channels`() = runTest {
        setup()
        val channels = listOf(
            Channel(1, 42, "general", ChannelKind.voice, 0),
            Channel(2, 42, "news", ChannelKind.text, 1),
        )
        serverRepository.listChannelsResult = Result.success(channels)
        vm.onIntent(AppIntent.SelectServer(42))

        assertEquals(42, vm.state.value.selectedServerId)
        assertEquals(listOf(42), serverRepository.listChannelsCalls)
        assertEquals(channels, vm.state.value.channels)
    }

    @Test
    fun `select server clears previous channel selection`() = runTest {
        setup()
        serverRepository.listChannelsResult = Result.success(
            listOf(Channel(1, 1, "voice", ChannelKind.voice, 0))
        )
        vm.onIntent(AppIntent.SelectServer(1))
        vm.onIntent(AppIntent.SelectChannel(1))
        // now select a different server
        serverRepository.listChannelsResult = Result.success(emptyList())
        vm.onIntent(AppIntent.SelectServer(2))

        assertNull(vm.state.value.selectedChannelId)
        assertTrue(vm.state.value.channels.isEmpty())
    }

    @Test
    fun `select server api failure stores error`() = runTest {
        setup()
        serverRepository.listChannelsResult = Result.failure(RuntimeException("Not found"))
        vm.onIntent(AppIntent.SelectServer(99))

        assertEquals("Not found", vm.state.value.apiError)
    }

    // -- SelectChannel intent -------------------------------------------------

    @Test
    fun `select voice channel connects to signaling server`() = runTest {
        val cfg = AppConfig(serverHost = "chat.local", serverPort = 9000)
        setup(cfg)
        serverRepository.listChannelsResult = Result.success(
            listOf(Channel(7, 1, "voice", ChannelKind.voice, 0))
        )
        vm.onIntent(AppIntent.SelectServer(1))
        vm.onIntent(AppIntent.SelectChannel(7))

        assertEquals(7, vm.state.value.selectedChannelId)
        assertTrue(connectionService.connectCalls.any { it.contains("/channels/7") })
    }

    @Test
    fun `select text channel does not connect`() = runTest {
        setup()
        serverRepository.listChannelsResult = Result.success(
            listOf(Channel(3, 1, "news", ChannelKind.text, 0))
        )
        vm.onIntent(AppIntent.SelectServer(1))
        vm.onIntent(AppIntent.SelectChannel(3))

        assertTrue(connectionService.connectCalls.isEmpty())
    }

    @Test
    fun `select unknown channel id is ignored`() = runTest {
        setup()
        vm.onIntent(AppIntent.SelectChannel(999))

        assertTrue(connectionService.connectCalls.isEmpty())
        assertNull(vm.state.value.selectedChannelId)
    }

    // -- CreateServer intent --------------------------------------------------

    @Test
    fun `create server appends to server list and closes dialog`() = runTest {
        setup()
        authRepository.loginResult = Result.success(Unit)
        serverRepository.listServersResult = Result.success(emptyList())
        vm.onIntent(AppIntent.Login("alice", "pass"))

        val newServer = Server(5, "My Guild", null, 1)
        serverRepository.createServerResult = Result.success(newServer)
        vm.onIntent(AppIntent.OpenCreateServerDialog)
        vm.onIntent(AppIntent.CreateServer("My Guild"))

        assertEquals(listOf("My Guild"), serverRepository.createServerCalls)
        assertTrue(vm.state.value.servers.any { it.id == 5 })
        assertFalse(vm.state.value.showCreateServerDialog)
    }

    @Test
    fun `create server failure stores error and keeps dialog open`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenCreateServerDialog)
        serverRepository.createServerResult = Result.failure(RuntimeException("Forbidden"))
        vm.onIntent(AppIntent.CreateServer("Evil"))

        assertEquals("Forbidden", vm.state.value.apiError)
        assertTrue(vm.state.value.showCreateServerDialog)
    }

    // -- Create server dialog -------------------------------------------------

    @Test
    fun `open create server dialog sets flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenCreateServerDialog)
        assertTrue(vm.state.value.showCreateServerDialog)
    }

    @Test
    fun `dismiss create server dialog clears flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenCreateServerDialog)
        vm.onIntent(AppIntent.DismissCreateServerDialog)
        assertFalse(vm.state.value.showCreateServerDialog)
    }

    // -- CreateChannel intent -------------------------------------------------

    @Test
    fun `create channel appends to channel list and closes dialog`() = runTest {
        setup()
        serverRepository.listChannelsResult = Result.success(emptyList())
        vm.onIntent(AppIntent.SelectServer(1))

        val newChannel = Channel(10, 1, "lobby", ChannelKind.voice, 0)
        serverRepository.createChannelResult = Result.success(newChannel)
        vm.onIntent(AppIntent.OpenCreateChannelDialog())
        vm.onIntent(AppIntent.CreateChannel("lobby", ChannelKind.voice))

        assertEquals(listOf(Triple(1, "lobby", ChannelKind.voice)), serverRepository.createChannelCalls)
        assertTrue(vm.state.value.channels.any { it.id == 10 })
        assertFalse(vm.state.value.showCreateChannelDialog)
    }

    @Test
    fun `create channel without selected server is ignored`() = runTest {
        setup()
        vm.onIntent(AppIntent.CreateChannel("orphan", ChannelKind.voice))
        assertTrue(serverRepository.createChannelCalls.isEmpty())
    }

    @Test
    fun `create channel failure stores error and keeps dialog open`() = runTest {
        setup()
        serverRepository.listChannelsResult = Result.success(emptyList())
        vm.onIntent(AppIntent.SelectServer(1))
        vm.onIntent(AppIntent.OpenCreateChannelDialog())
        serverRepository.createChannelResult = Result.failure(RuntimeException("Forbidden"))
        vm.onIntent(AppIntent.CreateChannel("blocked", ChannelKind.voice))

        assertEquals("Forbidden", vm.state.value.apiError)
        assertTrue(vm.state.value.showCreateChannelDialog)
    }

    @Test
    fun `created channel is sorted with voice before text`() = runTest {
        setup()
        serverRepository.listChannelsResult = Result.success(
            listOf(Channel(1, 1, "text-ch", ChannelKind.text, 0))
        )
        vm.onIntent(AppIntent.SelectServer(1))

        val voiceChannel = Channel(2, 1, "voice-ch", ChannelKind.voice, 0)
        serverRepository.createChannelResult = Result.success(voiceChannel)
        vm.onIntent(AppIntent.CreateChannel("voice-ch", ChannelKind.voice))

        assertEquals(ChannelKind.voice, vm.state.value.channels.first().kind)
    }

    // -- Create channel dialog ------------------------------------------------

    @Test
    fun `open create channel dialog sets flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenCreateChannelDialog())
        assertTrue(vm.state.value.showCreateChannelDialog)
    }

    @Test
    fun `dismiss create channel dialog clears flag`() = runTest {
        setup()
        vm.onIntent(AppIntent.OpenCreateChannelDialog())
        vm.onIntent(AppIntent.DismissCreateChannelDialog)
        assertFalse(vm.state.value.showCreateChannelDialog)
    }
}
