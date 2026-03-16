#pragma once

#include <jni.h>

#include "audio_transport.hpp"
#include "audio/audio_receiver.hpp"
#include "transport_jni.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

struct AudioTransportJni {
    AudioTransport channel;

    std::mutex recv_mutex;
    std::unordered_map<std::string, AudioReceiver*> voice_recv;
    AudioReceiver* screen_audio_recv = nullptr;

    explicit AudioTransportJni(TransportJni& t) ;

    void register_voice(const std::string& peer_id, AudioReceiver* recv);
    void unregister_voice(const std::string& peer_id);
    void set_screen_audio_recv(AudioReceiver* recv);
};
