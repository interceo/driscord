package com.driscord.data.audio

import kotlinx.coroutines.flow.StateFlow

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
    fun destroy()
}
