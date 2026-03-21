#pragma once

#include "audio/audio.hpp"
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

    void register_voice(const std::string& peer_id, std::shared_ptr<AudioReceiver> recv);
    void unregister_voice(const std::string& peer_id);
    void set_screen_audio_recv(const std::string& peer_id, std::shared_ptr<AudioReceiver> recv);
    void unset_screen_audio_recv(const std::string& peer_id);

private:
    Transport& transport_;

    std::mutex recv_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>> voice_recv_;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>> screen_audio_recv_;
};
