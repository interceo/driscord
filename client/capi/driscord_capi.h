#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Callback types — used by driscord_set_on_* functions.
 * Callbacks may be invoked from background threads.
 * --------------------------------------------------------------------------- */
typedef void (*DriscordStringCb)(const char* value);
typedef void (*DriscordFrameCb)(const char* peer_id, const uint8_t* rgba, int w, int h);

/* ---------------------------------------------------------------------------
 * Memory management
 * String-returning functions allocate with strdup() (malloc-based).
 * driscord_capture_grab_thumbnail allocates with malloc().
 * All returned pointers must be freed with the standard C free().
 * --------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Transport
 * --------------------------------------------------------------------------- */
void        driscord_add_turn_server(const char* url, const char* user, const char* pass);
void        driscord_connect(const char* url);
void        driscord_disconnect(void);
bool        driscord_connected(void);
const char* driscord_local_id(void);   /* free with driscord_free_str */
const char* driscord_peers(void);      /* free with driscord_free_str — JSON array */

void driscord_set_on_peer_joined(DriscordStringCb cb);
void driscord_set_on_peer_left(DriscordStringCb cb);
void driscord_set_on_streaming_started(DriscordStringCb cb);
void driscord_set_on_streaming_stopped(DriscordStringCb cb);

/* ---------------------------------------------------------------------------
 * Audio
 * --------------------------------------------------------------------------- */
void  driscord_audio_send(const uint8_t* data, int len);
bool  driscord_audio_start(void);
void  driscord_audio_stop(void);
bool  driscord_audio_deafened(void);
void  driscord_audio_set_deafened(bool deaf);
float driscord_audio_master_volume(void);
void  driscord_audio_set_master_volume(float vol);
float driscord_audio_output_level(void);
bool  driscord_audio_self_muted(void);
void  driscord_audio_set_self_muted(bool muted);
float driscord_audio_input_level(void);

const char* driscord_audio_list_input_devices(void);   /* free with driscord_free_str */
void        driscord_audio_set_input_device(const char* id);
const char* driscord_audio_list_output_devices(void);  /* free with driscord_free_str */
void        driscord_audio_set_output_device(const char* id);

void  driscord_audio_on_peer_joined(const char* peer, int jitter_ms);
void  driscord_audio_on_peer_left(const char* peer);
void  driscord_audio_set_peer_volume(const char* peer, float vol);
float driscord_audio_get_peer_volume(const char* peer);
void  driscord_audio_set_peer_muted(const char* peer, bool muted);
bool  driscord_audio_get_peer_muted(const char* peer);

void driscord_audio_set_screen_audio_receiver(const char* peer, int64_t screen_handle);
void driscord_audio_unset_screen_audio_receiver(const char* peer);
void driscord_audio_add_screen_audio_to_mixer(const char* peer);
void driscord_audio_remove_screen_audio_from_mixer(const char* peer);

/* ---------------------------------------------------------------------------
 * Video
 * --------------------------------------------------------------------------- */
void driscord_video_set_watching(bool watching);
bool driscord_video_watching(void);
void driscord_video_remove_streaming_peer(const char* peer);
void driscord_video_send_keyframe_request(void);

void driscord_set_on_new_streaming_peer(DriscordStringCb cb);
void driscord_set_on_streaming_peer_removed(DriscordStringCb cb);

/* ---------------------------------------------------------------------------
 * Capture
 * --------------------------------------------------------------------------- */
bool        driscord_capture_system_audio_available(void);
const char* driscord_capture_video_list_targets(void); /* free with driscord_free_str */

/* Returns byte count written to *out_rgba (malloc'd), or -1 on failure.
 * *out_rgba must be freed with driscord_free_buf(). */
int driscord_capture_grab_thumbnail(const char* target_json, int max_w, int max_h,
                                    uint8_t** out_rgba);

/* ---------------------------------------------------------------------------
 * Screen session
 * --------------------------------------------------------------------------- */
void driscord_screen_init(int buf_ms, int max_sync_ms);
void driscord_screen_deinit(void);

bool driscord_screen_start_sharing(const char* target_json,
                                   int max_w, int max_h,
                                   int fps, int bitrate_kbps,
                                   int gop_size, bool share_audio);
void driscord_screen_stop_sharing(void);
bool driscord_screen_sharing(void);
bool driscord_screen_sharing_audio(void);
void driscord_screen_force_keyframe(void);
void driscord_screen_update(void);

const char* driscord_screen_active_peer(void); /* free with driscord_free_str */
bool        driscord_screen_active(void);
void        driscord_screen_reset(void);
void        driscord_screen_reset_audio_receiver(void);

void  driscord_screen_set_stream_volume(const char* peer, float vol);
float driscord_screen_stream_volume(void);
const char* driscord_screen_stats(void); /* free with driscord_free_str — JSON */

void driscord_set_on_frame(DriscordFrameCb cb);
void driscord_set_on_frame_removed(DriscordStringCb cb);

void driscord_screen_join_stream(const char* peer_id);
void driscord_screen_leave_stream(void);

#ifdef __cplusplus
}
#endif
