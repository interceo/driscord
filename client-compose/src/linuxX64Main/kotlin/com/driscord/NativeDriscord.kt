package com.driscord

import driscord.capi.*
import kotlinx.cinterop.*

/**
 * Kotlin/Native wrapper around the C API (libdriscord_capi.so).
 * Mirrors the JVM NativeDriscord object but uses cinterop instead of JNI.
 *
 * Callback design: the C++ core calls back from its own threads.
 * Use [staticCFunction] for top-level C callbacks; forward to Kotlin via
 * global [NativeCallbacks] so that lambdas can capture state.
 */
@OptIn(ExperimentalForeignApi::class)
object NativeDriscord {

    // -- Transport --

    fun addTurnServer(url: String, user: String, pass: String) =
        driscord_add_turn_server(url, user, pass)

    fun connect(url: String) = driscord_connect(url)
    fun disconnect() = driscord_disconnect()
    fun connected(): Boolean = driscord_connected()

    fun localId(): String = memScoped {
        val raw = driscord_local_id() ?: return ""
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    fun peers(): String = memScoped {
        val raw = driscord_peers() ?: return "[]"
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    fun setOnPeerJoined(cb: (String) -> Unit) {
        NativeCallbacks.onPeerJoined = cb
        driscord_set_on_peer_joined(staticCFunction { peer ->
            NativeCallbacks.onPeerJoined?.invoke(peer?.toKString() ?: "")
        })
    }

    fun setOnPeerLeft(cb: (String) -> Unit) {
        NativeCallbacks.onPeerLeft = cb
        driscord_set_on_peer_left(staticCFunction { peer ->
            NativeCallbacks.onPeerLeft?.invoke(peer?.toKString() ?: "")
        })
    }

    fun setOnStreamingStarted(cb: (String) -> Unit) {
        NativeCallbacks.onStreamingStarted = cb
        driscord_set_on_streaming_started(staticCFunction { peer ->
            NativeCallbacks.onStreamingStarted?.invoke(peer?.toKString() ?: "")
        })
    }

    fun setOnStreamingStopped(cb: (String) -> Unit) {
        NativeCallbacks.onStreamingStopped = cb
        driscord_set_on_streaming_stopped(staticCFunction { peer ->
            NativeCallbacks.onStreamingStopped?.invoke(peer?.toKString() ?: "")
        })
    }

    // -- Audio --

    fun audioStart(): Boolean = driscord_audio_start()
    fun audioStop() = driscord_audio_stop()
    fun audioDeafened(): Boolean = driscord_audio_deafened()
    fun audioSetDeafened(deaf: Boolean) = driscord_audio_set_deafened(deaf)
    fun audioMasterVolume(): Float = driscord_audio_master_volume()
    fun audioSetMasterVolume(vol: Float) = driscord_audio_set_master_volume(vol)
    fun audioOutputLevel(): Float = driscord_audio_output_level()
    fun audioSelfMuted(): Boolean = driscord_audio_self_muted()
    fun audioSetSelfMuted(muted: Boolean) = driscord_audio_set_self_muted(muted)
    fun audioInputLevel(): Float = driscord_audio_input_level()

    fun audioListInputDevices(): String = memScoped {
        val raw = driscord_audio_list_input_devices() ?: return "[]"
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    fun audioSetInputDevice(id: String) = driscord_audio_set_input_device(id)

    fun audioListOutputDevices(): String = memScoped {
        val raw = driscord_audio_list_output_devices() ?: return "[]"
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    fun audioSetOutputDevice(id: String) = driscord_audio_set_output_device(id)
    fun audioOnPeerJoined(peer: String, jitterMs: Int) = driscord_audio_on_peer_joined(peer, jitterMs)
    fun audioOnPeerLeft(peer: String) = driscord_audio_on_peer_left(peer)
    fun audioSetPeerVolume(peer: String, vol: Float) = driscord_audio_set_peer_volume(peer, vol)
    fun audioGetPeerVolume(peer: String): Float = driscord_audio_get_peer_volume(peer)
    fun audioSetPeerMuted(peer: String, muted: Boolean) = driscord_audio_set_peer_muted(peer, muted)
    fun audioGetPeerMuted(peer: String): Boolean = driscord_audio_get_peer_muted(peer)
    fun audioSetScreenAudioReceiver(peerId: String, screenHandle: Long) =
        driscord_audio_set_screen_audio_receiver(peerId, screenHandle)
    fun audioUnsetScreenAudioReceiver(peerId: String) = driscord_audio_unset_screen_audio_receiver(peerId)
    fun audioAddScreenAudioToMixer(peerId: String) = driscord_audio_add_screen_audio_to_mixer(peerId)
    fun audioRemoveScreenAudioFromMixer(peerId: String) = driscord_audio_remove_screen_audio_from_mixer(peerId)

    // -- Video --

    fun videoSetWatching(watching: Boolean) = driscord_video_set_watching(watching)
    fun videoWatching(): Boolean = driscord_video_watching()
    fun videoRemoveStreamingPeer(peer: String) = driscord_video_remove_streaming_peer(peer)
    fun videoSendKeyframeRequest() = driscord_video_send_keyframe_request()

    fun setOnNewStreamingPeer(cb: (String) -> Unit) {
        NativeCallbacks.onNewStreamingPeer = cb
        driscord_set_on_new_streaming_peer(staticCFunction { peer ->
            NativeCallbacks.onNewStreamingPeer?.invoke(peer?.toKString() ?: "")
        })
    }

    fun setOnStreamingPeerRemoved(cb: (String) -> Unit) {
        NativeCallbacks.onStreamingPeerRemoved = cb
        driscord_set_on_streaming_peer_removed(staticCFunction { peer ->
            NativeCallbacks.onStreamingPeerRemoved?.invoke(peer?.toKString() ?: "")
        })
    }

    // -- Capture --

    fun captureSystemAudioAvailable(): Boolean = driscord_capture_system_audio_available()

    fun captureVideoListTargets(): String = memScoped {
        val raw = driscord_capture_video_list_targets() ?: return "[]"
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    // -- Screen --

    fun screenInit(bufMs: Int, maxSyncMs: Int) = driscord_screen_init(bufMs, maxSyncMs)
    fun screenDeinit() = driscord_screen_deinit()

    fun screenStartSharing(
        targetJson: String,
        maxW: Int, maxH: Int,
        fps: Int, bitrateKbps: Int,
        gopSize: Int, shareAudio: Boolean,
    ): Boolean = driscord_screen_start_sharing(targetJson, maxW, maxH, fps, bitrateKbps, gopSize, shareAudio)

    fun screenStopSharing() = driscord_screen_stop_sharing()
    fun screenSharing(): Boolean = driscord_screen_sharing()
    fun screenSharingAudio(): Boolean = driscord_screen_sharing_audio()
    fun screenForceKeyframe() = driscord_screen_force_keyframe()
    fun screenUpdate() = driscord_screen_update()

    fun screenActivePeer(): String = memScoped {
        val raw = driscord_screen_active_peer() ?: return ""
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    fun screenActive(): Boolean = driscord_screen_active()
    fun screenReset() = driscord_screen_reset()
    fun screenResetAudioReceiver() = driscord_screen_reset_audio_receiver()
    fun screenSetStreamVolume(peerId: String, vol: Float) = driscord_screen_set_stream_volume(peerId, vol)
    fun screenStreamVolume(): Float = driscord_screen_stream_volume()

    fun screenStats(): String = memScoped {
        val raw = driscord_screen_stats() ?: return "{}"
        val result = raw.toKString()
        driscord_free_str(raw)
        result
    }

    fun setOnFrameRemoved(cb: (String) -> Unit) {
        NativeCallbacks.onFrameRemoved = cb
        driscord_set_on_frame_removed(staticCFunction { peer ->
            NativeCallbacks.onFrameRemoved?.invoke(peer?.toKString() ?: "")
        })
    }

    fun screenJoinStream(peerId: String) = driscord_screen_join_stream(peerId)
    fun screenLeaveStream() = driscord_screen_leave_stream()
}

/**
 * Global callback storage — required because [staticCFunction] lambdas cannot capture variables.
 */
object NativeCallbacks {
    var onPeerJoined: ((String) -> Unit)? = null
    var onPeerLeft: ((String) -> Unit)? = null
    var onStreamingStarted: ((String) -> Unit)? = null
    var onStreamingStopped: ((String) -> Unit)? = null
    var onNewStreamingPeer: ((String) -> Unit)? = null
    var onStreamingPeerRemoved: ((String) -> Unit)? = null
    var onFrameRemoved: ((String) -> Unit)? = null
}
