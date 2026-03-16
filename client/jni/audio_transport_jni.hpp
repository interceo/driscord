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

    explicit AudioTransportJni(TransportJni& t) : channel(t.transport) {
        channel.on_audio_received([this](const std::string& peer_id,
                                         const uint8_t* data, size_t len) {
            std::scoped_lock lk(recv_mutex);
            auto it = voice_recv.find(peer_id);
            if (it != voice_recv.end()) it->second->push_packet(data, len);
        });
        channel.on_screen_audio_received([this](const std::string&,
                                                 const uint8_t* data, size_t len) {
            std::scoped_lock lk(recv_mutex);
            if (screen_audio_recv) screen_audio_recv->push_packet(data, len);
        });
    }

    void register_voice(const std::string& peer_id, AudioReceiver* recv) {
        std::scoped_lock lk(recv_mutex);
        voice_recv[peer_id] = recv;
    }
    void unregister_voice(const std::string& peer_id) {
        std::scoped_lock lk(recv_mutex);
        voice_recv.erase(peer_id);
    }
    void set_screen_audio_recv(AudioReceiver* recv) {
        std::scoped_lock lk(recv_mutex);
        screen_audio_recv = recv;
    }
};
