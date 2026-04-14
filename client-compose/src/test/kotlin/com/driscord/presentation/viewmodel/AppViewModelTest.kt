package com.driscord.presentation.viewmodel

import app.cash.turbine.turbineScope
import com.driscord.AppConfig
import com.driscord.data.FakeAudioService
import com.driscord.data.FakeConfigRepository
import com.driscord.data.FakeConnectionService
import com.driscord.data.FakeVideoService
import com.driscord.domain.model.CaptureTarget
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
    private lateinit var vm: AppViewModel

    private fun setup(config: AppConfig = AppConfig()): TestScope {
        val testScope = TestScope(UnconfinedTestDispatcher())
        connectionService = FakeConnectionService()
        audioService = FakeAudioService()
        videoService = FakeVideoService()
        configRepository = FakeConfigRepository(config)
        vm = AppViewModel(
            connectionService, audioService, videoService, configRepository,
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
}
