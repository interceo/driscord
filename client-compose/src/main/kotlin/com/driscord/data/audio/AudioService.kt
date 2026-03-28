package com.driscord.data.audio

import kotlinx.coroutines.flow.StateFlow

data class AudioInputDevice(val id: String, val name: String)

interface AudioService {
    val muted: StateFlow<Boolean>
    val deafened: StateFlow<Boolean>
    val outputVolume: StateFlow<Float>
    val inputLevel: StateFlow<Float>

    fun start()
    fun stop()
    fun toggleMute()
    fun toggleDeafen()
    fun setOutputVolume(vol: Float)
    fun setPeerVolume(peerId: String, vol: Float)
    fun getPeerVolume(peerId: String): Float
    fun onPeerJoined(peerId: String, jitterMs: Int)
    fun onPeerLeft(peerId: String)

    fun listInputDevices(): List<AudioInputDevice>
    fun setInputDevice(id: String)

    fun listOutputDevices(): List<AudioInputDevice>
    fun setOutputDevice(id: String)

    fun destroy()
}
