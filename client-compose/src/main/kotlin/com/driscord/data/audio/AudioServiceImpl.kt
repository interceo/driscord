package com.driscord.data.audio

import com.driscord.jni.NativeDriscord
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

class AudioServiceImpl : AudioService {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val _muted = MutableStateFlow(false)
    override val muted: StateFlow<Boolean> = _muted.asStateFlow()

    private val _deafened = MutableStateFlow(false)
    override val deafened: StateFlow<Boolean> = _deafened.asStateFlow()

    private val _outputVolume = MutableStateFlow(1.0f)
    override val outputVolume: StateFlow<Float> = _outputVolume.asStateFlow()

    private val _inputLevel = MutableStateFlow(0.0f)
    override val inputLevel: StateFlow<Float> = _inputLevel.asStateFlow()

    init {
        scope.launch {
            while (isActive) {
                withContext(Dispatchers.Main) {
                    _muted.value = NativeDriscord.audioSelfMuted()
                    _deafened.value = NativeDriscord.audioDeafened()
                    _inputLevel.value = NativeDriscord.audioInputLevel()
                }
                delay(16)
            }
        }
    }

    override fun start(voiceBitrateKbps: Int) {
        val err = NativeDriscord.audioStart(voiceBitrateKbps)
        if (err != null) {
            System.err.println("AudioService: audioStart failed: $err")
        }
    }

    override fun stop() {
        NativeDriscord.audioStop()
    }

    override fun setNoiseGate(threshold: Float) {
        NativeDriscord.audioSetNoiseGate(threshold)
    }

    override fun toggleMute() {
        val next = !NativeDriscord.audioSelfMuted()
        NativeDriscord.audioSetSelfMuted(next)
        _muted.value = next
    }

    override fun toggleDeafen() {
        val next = !NativeDriscord.audioDeafened()
        NativeDriscord.audioSetDeafened(next)
        _deafened.value = next
        NativeDriscord.audioSetSelfMuted(next)
        _muted.value = NativeDriscord.audioSelfMuted()
    }

    override fun setOutputVolume(vol: Float) {
        NativeDriscord.audioSetMasterVolume(vol)
        _outputVolume.value = vol
    }

    override fun setPeerVolume(peerId: String, vol: Float) {
        NativeDriscord.audioSetPeerVolume(peerId, vol)
    }

    override fun getPeerVolume(peerId: String): Float =
        NativeDriscord.audioGetPeerVolume(peerId)

    override fun onPeerJoined(peerId: String, jitterMs: Int) {
        NativeDriscord.audioOnPeerJoined(peerId, jitterMs)
    }

    override fun onPeerLeft(peerId: String) {
        NativeDriscord.audioOnPeerLeft(peerId)
    }

    override fun listInputDevices(): List<AudioInputDevice> {
        @Serializable data class DeviceJson(val id: String, val name: String)
        return runCatching {
            Json.decodeFromString<List<DeviceJson>>(NativeDriscord.audioListInputDevices())
                .map { AudioInputDevice(it.id, it.name) }
        }.getOrElse { emptyList() }
    }

    override fun setInputDevice(id: String) {
        NativeDriscord.audioSetInputDevice(id)
    }

    override fun listOutputDevices(): List<AudioInputDevice> {
        @Serializable data class DeviceJson(val id: String, val name: String)
        return runCatching {
            Json.decodeFromString<List<DeviceJson>>(NativeDriscord.audioListOutputDevices())
                .map { AudioInputDevice(it.id, it.name) }
        }.getOrElse { emptyList() }
    }

    override fun setOutputDevice(id: String) {
        NativeDriscord.audioSetOutputDevice(id)
    }

    override fun destroy() {
        scope.cancel()
        stop()
    }
}
