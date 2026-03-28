#include "driscord_capi.h"
#include "../jni/driscord_state.hpp"

#include <cstring>

#define CORE() DriscordState::get().core

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static const char* dup_str(const std::string& s) {
    char* out = new char[s.size() + 1];
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

static DriscordCore::StringCb wrap_str_cb(DriscordStringCb cb) {
    if (!cb) return nullptr;
    return [cb](const std::string& s) { cb(s.c_str()); };
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

void driscord_free_str(const char* s) { delete[] s; }
void driscord_free_buf(uint8_t* buf)  { delete[] buf; }

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void driscord_add_turn_server(const char* url, const char* user, const char* pass) {
    CORE().add_turn_server(url, user, pass);
}
void driscord_connect(const char* url)   { CORE().connect(url); }
void driscord_disconnect(void)           { CORE().disconnect(); }
bool driscord_connected(void)            { return CORE().connected(); }

const char* driscord_local_id(void) { return dup_str(CORE().local_id()); }
const char* driscord_peers(void)    { return dup_str(CORE().peers_json()); }

void driscord_set_on_peer_joined(DriscordStringCb cb)       { CORE().set_on_peer_joined(wrap_str_cb(cb)); }
void driscord_set_on_peer_left(DriscordStringCb cb)         { CORE().set_on_peer_left(wrap_str_cb(cb)); }
void driscord_set_on_streaming_started(DriscordStringCb cb) { CORE().set_on_streaming_started(wrap_str_cb(cb)); }
void driscord_set_on_streaming_stopped(DriscordStringCb cb) { CORE().set_on_streaming_stopped(wrap_str_cb(cb)); }

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void  driscord_audio_send(const uint8_t* data, int len) { CORE().audio_send(data, len); }
bool  driscord_audio_start(void)                        { return CORE().audio_start(); }
void  driscord_audio_stop(void)                         { CORE().audio_stop(); }
bool  driscord_audio_deafened(void)                     { return CORE().audio_deafened(); }
void  driscord_audio_set_deafened(bool deaf)            { CORE().audio_set_deafened(deaf); }
float driscord_audio_master_volume(void)                { return CORE().audio_master_volume(); }
void  driscord_audio_set_master_volume(float vol)       { CORE().audio_set_master_volume(vol); }
float driscord_audio_output_level(void)                 { return CORE().audio_output_level(); }
bool  driscord_audio_self_muted(void)                   { return CORE().audio_self_muted(); }
void  driscord_audio_set_self_muted(bool muted)         { CORE().audio_set_self_muted(muted); }
float driscord_audio_input_level(void)                  { return CORE().audio_input_level(); }

const char* driscord_audio_list_input_devices(void)  { return dup_str(CORE().audio_list_input_devices_json()); }
void        driscord_audio_set_input_device(const char* id)  { CORE().audio_set_input_device(id); }
const char* driscord_audio_list_output_devices(void) { return dup_str(CORE().audio_list_output_devices_json()); }
void        driscord_audio_set_output_device(const char* id) { CORE().audio_set_output_device(id); }

void  driscord_audio_on_peer_joined(const char* peer, int jitter_ms) { CORE().audio_on_peer_joined(peer, jitter_ms); }
void  driscord_audio_on_peer_left(const char* peer)                  { CORE().audio_on_peer_left(peer); }
void  driscord_audio_set_peer_volume(const char* peer, float vol)    { CORE().audio_set_peer_volume(peer, vol); }
float driscord_audio_get_peer_volume(const char* peer)               { return CORE().audio_peer_volume(peer); }
void  driscord_audio_set_peer_muted(const char* peer, bool muted)    { CORE().audio_set_peer_muted(peer, muted); }
bool  driscord_audio_get_peer_muted(const char* peer)                { return CORE().audio_peer_muted(peer); }

void driscord_audio_set_screen_audio_receiver(const char* peer, int64_t screen_handle) {
    CORE().audio_set_screen_audio_receiver(peer, screen_handle != 0);
}
void driscord_audio_unset_screen_audio_receiver(const char* peer)    { CORE().audio_unset_screen_audio_receiver(peer); }
void driscord_audio_add_screen_audio_to_mixer(const char* peer)      { CORE().audio_add_screen_audio_to_mixer(peer); }
void driscord_audio_remove_screen_audio_from_mixer(const char* peer) { CORE().audio_remove_screen_audio_from_mixer(peer); }

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void driscord_video_set_watching(bool watching)              { CORE().video_set_watching(watching); }
bool driscord_video_watching(void)                           { return CORE().video_watching(); }
void driscord_video_remove_streaming_peer(const char* peer)  { CORE().video_remove_streaming_peer(peer); }
void driscord_video_send_keyframe_request(void)              { CORE().video_send_keyframe_request(); }

void driscord_set_on_new_streaming_peer(DriscordStringCb cb)      { CORE().set_on_new_streaming_peer(wrap_str_cb(cb)); }
void driscord_set_on_streaming_peer_removed(DriscordStringCb cb)  { CORE().set_on_streaming_peer_removed(wrap_str_cb(cb)); }

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

bool        driscord_capture_system_audio_available(void) { return CORE().capture_system_audio_available(); }
const char* driscord_capture_video_list_targets(void)     { return dup_str(CORE().capture_video_list_targets_json()); }

int driscord_capture_grab_thumbnail(const char* target_json, int max_w, int max_h,
                                    uint8_t** out_rgba) {
    auto rgba = CORE().capture_grab_thumbnail(target_json, max_w, max_h);
    if (rgba.empty()) {
        *out_rgba = nullptr;
        return -1;
    }
    *out_rgba = new uint8_t[rgba.size()];
    std::memcpy(*out_rgba, rgba.data(), rgba.size());
    return static_cast<int>(rgba.size());
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

void driscord_screen_init(int buf_ms, int max_sync_ms) { CORE().init_screen_session(buf_ms, max_sync_ms); }
void driscord_screen_deinit(void)                      { CORE().deinit_screen_session(); }

bool driscord_screen_start_sharing(const char* target_json,
                                   int max_w, int max_h,
                                   int fps, int bitrate_kbps,
                                   int /*gop_size*/, bool share_audio) {
    return CORE().screen_start_sharing(target_json, max_w, max_h, fps, bitrate_kbps, share_audio);
}

void        driscord_screen_stop_sharing(void)             { CORE().screen_stop_sharing(); }
bool        driscord_screen_sharing(void)                  { return CORE().screen_sharing(); }
bool        driscord_screen_sharing_audio(void)            { return CORE().screen_sharing_audio(); }
void        driscord_screen_force_keyframe(void)           { CORE().screen_force_keyframe(); }
void        driscord_screen_update(void)                   { CORE().screen_update(); }
const char* driscord_screen_active_peer(void)              { return dup_str(CORE().screen_active_peer()); }
bool        driscord_screen_active(void)                   { return CORE().screen_active(); }
void        driscord_screen_reset(void)                    { CORE().screen_reset(); }
void        driscord_screen_reset_audio_receiver(void)     { CORE().screen_reset_audio(); }

void  driscord_screen_set_stream_volume(const char* peer, float vol) { CORE().screen_set_stream_volume(peer, vol); }
float driscord_screen_stream_volume(void)                            { return CORE().screen_stream_volume(); }
const char* driscord_screen_stats(void)                              { return dup_str(CORE().screen_stats_json()); }

void driscord_set_on_frame(DriscordFrameCb cb) {
    if (!cb) {
        CORE().set_on_frame(nullptr);
        return;
    }
    CORE().set_on_frame([cb](const std::string& peer, const uint8_t* rgba, int w, int h) {
        cb(peer.c_str(), rgba, w, h);
    });
}

void driscord_set_on_frame_removed(DriscordStringCb cb) { CORE().set_on_frame_removed(wrap_str_cb(cb)); }
void driscord_screen_join_stream(const char* peer_id)   { CORE().join_stream(peer_id); }
void driscord_screen_leave_stream(void)                 { CORE().leave_stream(); }
