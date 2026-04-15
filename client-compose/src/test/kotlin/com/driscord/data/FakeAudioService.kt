package com.driscord.data

import com.driscord.data.audio.AudioDevice
import com.driscord.data.audio.AudioService
import kotlinx.coroutines.flow.MutableStateFlow

class FakeAudioService : AudioService {

    override val muted = MutableStateFlow(false)
    override val deafened = MutableStateFlow(false)
    override val outputVolume = MutableStateFlow(1.0f)
    override val inputLevel = MutableStateFlow(0.0f)

    var startCalled = 0
    var stopCalled = 0
    var destroyCalled = 0
    var lastNoiseGate: Float? = null
    var lastInputDevice: String? = null
    var lastOutputDevice: String? = null
    var peerVolumes = mutableMapOf<String, Float>()
    var peersJoined = mutableListOf<String>()
    var peersLeft = mutableListOf<String>()
    var toggleMuteCalled = 0
    var toggleDeafenCalled = 0
    var lastOutputVolume: Float? = null

    override fun start() { startCalled++ }
    override fun stop() { stopCalled++ }

    override fun setNoiseGate(threshold: Float) { lastNoiseGate = threshold }

    override fun toggleMute() {
        toggleMuteCalled++
        muted.value = !muted.value
    }

    override fun toggleDeafen() {
        toggleDeafenCalled++
        val next = !deafened.value
        deafened.value = next
        muted.value = next
    }

    override fun setOutputVolume(vol: Float) {
        lastOutputVolume = vol
        outputVolume.value = vol
    }

    override fun setPeerVolume(peerId: String, vol: Float) { peerVolumes[peerId] = vol }
    override fun getPeerVolume(peerId: String): Float = peerVolumes[peerId] ?: 1.0f

    override fun onPeerJoined(peerId: String) { peersJoined += peerId }
    override fun onPeerLeft(peerId: String) { peersLeft += peerId }

    override fun listInputDevices(): List<AudioDevice> = emptyList()
    override fun setInputDevice(id: String) { lastInputDevice = id }

    override fun listOutputDevices(): List<AudioDevice> = emptyList()
    override fun setOutputDevice(id: String) { lastOutputDevice = id }

    override fun destroy() { destroyCalled++ }
}
