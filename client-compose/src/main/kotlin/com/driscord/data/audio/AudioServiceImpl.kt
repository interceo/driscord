package com.driscord.data.audio

import com.driscord.jni.NativeAudioTransport
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

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
                    _muted.value = NativeAudioTransport.selfMuted()
                    _deafened.value = NativeAudioTransport.deafened()
                    _inputLevel.value = NativeAudioTransport.inputLevel()
                }
                delay(16)
            }
        }
    }

    override fun start() {
        NativeAudioTransport.start()
    }

    override fun stop() {
        NativeAudioTransport.stop()
    }

    override fun toggleMute() {
        val next = !NativeAudioTransport.selfMuted()
        NativeAudioTransport.setSelfMuted(next)
        _muted.value = next
    }

    override fun toggleDeafen() {
        val next = !NativeAudioTransport.deafened()
        NativeAudioTransport.setDeafened(next)
        _deafened.value = next
        NativeAudioTransport.setSelfMuted(next)
        _muted.value = NativeAudioTransport.selfMuted()
    }

    override fun setOutputVolume(vol: Float) {
        NativeAudioTransport.setMasterVolume(vol)
        _outputVolume.value = vol
    }

    override fun setPeerVolume(peerId: String, vol: Float) {
        NativeAudioTransport.setPeerVolume(peerId, vol)
    }

    override fun getPeerVolume(peerId: String): Float =
        NativeAudioTransport.getPeerVolume(peerId)

    override fun onPeerJoined(peerId: String, jitterMs: Int) {
        NativeAudioTransport.onPeerJoined(peerId, jitterMs)
    }

    override fun onPeerLeft(peerId: String) {
        NativeAudioTransport.onPeerLeft(peerId)
    }

    override fun destroy() {
        scope.cancel()
        stop()
    }
}
