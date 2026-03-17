#pragma once

#include <jni.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "audio_transport.hpp"
#include "audio/audio_receiver.hpp"
#include "transport_jni.hpp"

struct AudioTransportJni {
    AudioTransport channel;

    std::mutex recv_mutex;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>> voice_recv;
    std::shared_ptr<AudioReceiver> screen_audio_recv;

    explicit AudioTransportJni(TransportJni& t);

    void register_voice(const std::string& peer_id, std::shared_ptr<AudioReceiver> recv);
    void unregister_voice(const std::string& peer_id);
    void set_screen_audio_recv(std::shared_ptr<AudioReceiver> recv);
};
