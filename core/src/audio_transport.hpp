#pragma once

#include "audio/audio.hpp"
#include "audio/audio_mixer.hpp"
#include "transport.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class AudioTransport {
public:
    explicit AudioTransport(Transport& transport);

    void send_audio(const uint8_t* data, size_t len);
    void send_screen_audio(const uint8_t* data, size_t len);

    // Mixer lifecycle
    bool start();
    void stop();

    // Master controls
    void set_master_volume(float v);
    float master_volume() const;
    void set_deafened(bool d);
    bool deafened() const;
    float output_level() const;

    // Peer lifecycle — auto-creates/destroys AudioReceiver and mixer source
    void on_peer_joined(const std::string& peer_id, int jitter_ms);
    void on_peer_left(const std::string& peer_id);

    // Self (microphone) controls
    void set_self_muted(bool m);
    bool self_muted() const;
    float input_level() const;

    // Mic device selection
    static std::string list_input_devices_json();
    void set_input_device(std::string id);

    // Output device selection
    static std::string list_output_devices_json();
    void set_output_device(std::string id);

    // Per-peer voice controls
    void set_peer_volume(const std::string& peer_id, float v);
    float peer_volume(const std::string& peer_id) const;
    void set_peer_muted(const std::string& peer_id, bool muted);
    bool peer_muted(const std::string& peer_id) const;

    // Screen audio
    void set_screen_audio_recv(const std::string& peer_id, std::shared_ptr<AudioReceiver> recv);
    void unset_screen_audio_recv(const std::string& peer_id);
    void add_screen_audio_to_mixer(const std::string& peer_id);
    void remove_screen_audio_from_mixer(const std::string& peer_id);

    // Per-peer screen audio controls
    void  set_screen_audio_peer_volume(const std::string& peer_id, float v);
    float screen_audio_peer_volume(const std::string& peer_id) const;
    void  set_screen_audio_peer_muted(const std::string& peer_id, bool muted);
    bool  screen_audio_peer_muted(const std::string& peer_id) const;

private:
    Transport& transport_;
    AudioMixer  mixer_;
    AudioSender sender_;

    mutable std::mutex recv_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>> voice_recv_;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>> screen_audio_recv_;
};
