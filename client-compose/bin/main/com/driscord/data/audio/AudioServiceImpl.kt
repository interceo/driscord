package com.driscord.data.audio

import com.driscord.jni.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class AudioServiceImpl(
    private val audioTransportH: Long,
) : AudioService {

    private val audioSenderH: Long = NativeAudioSender.create(audioTransportH)
    private val audioMixerH: Long = NativeAudioMixer.create()

    override val mixerHandle: Long get() = audioMixerH

    private val voiceReceivers = HashMap<String, Long>()
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
        // Polling loop scoped to audio only
        scope.launch {
            while (isActive) {
                withContext(Dispatchers.Main) {
                    _muted.value = NativeAudioSender.muted(audioSenderH)
                    _deafened.value = NativeAudioMixer.deafened(audioMixerH)
                    _inputLevel.value = NativeAudioSender.inputLevel(audioSenderH)
                }
                delay(16)
            }
        }
    }

    override fun start() {
        NativeAudioMixer.start(audioMixerH)
        NativeAudioSender.start(audioSenderH)
    }

    override fun stop() {
        NativeAudioSender.stop(audioSenderH)
        NativeAudioMixer.stop(audioMixerH)
    }

    override fun toggleMute() {
        val next = !NativeAudioSender.muted(audioSenderH)
        NativeAudioSender.setMuted(audioSenderH, next)
        _muted.value = next
    }

    override fun toggleDeafen() {
        val next = !NativeAudioMixer.deafened(audioMixerH)
        NativeAudioMixer.setDeafened(audioMixerH, next)
        _deafened.value = next
        NativeAudioSender.setMuted(audioSenderH, next)
        _muted.value = NativeAudioSender.muted(audioSenderH)
    }

    override fun setOutputVolume(vol: Float) {
        NativeAudioMixer.setOutputVolume(audioMixerH, vol)
        _outputVolume.value = vol
    }

    override fun setPeerVolume(peerId: String, vol: Float) {
        val recv = synchronized(voiceReceivers) { voiceReceivers[peerId] } ?: return
        NativeAudioReceiver.setVolume(recv, vol)
    }

    override fun getPeerVolume(peerId: String): Float {
        val recv = synchronized(voiceReceivers) { voiceReceivers[peerId] } ?: return 1f
        return NativeAudioReceiver.volume(recv)
    }

    override fun onPeerJoined(peerId: String, jitterMs: Int) {
        val recv = NativeAudioReceiver.create(jitterMs)
        synchronized(voiceReceivers) { voiceReceivers[peerId] = recv }
        NativeAudioTransport.registerVoiceReceiver(audioTransportH, peerId, recv)
        NativeAudioMixer.addSource(audioMixerH, recv)
    }

    override fun onPeerLeft(peerId: String) {
        val recv = synchronized(voiceReceivers) { voiceReceivers.remove(peerId) } ?: return
        NativeAudioTransport.unregisterVoiceReceiver(audioTransportH, peerId)
        NativeAudioMixer.removeSource(audioMixerH, recv)
        NativeAudioReceiver.destroy(recv)
    }

    override fun destroy() {
        scope.cancel()
        stop()
        synchronized(voiceReceivers) {
            voiceReceivers.values.forEach { recv ->
                NativeAudioMixer.removeSource(audioMixerH, recv)
                NativeAudioReceiver.destroy(recv)
            }
            voiceReceivers.clear()
        }
        NativeAudioSender.destroy(audioSenderH)
        NativeAudioMixer.destroy(audioMixerH)
    }
}