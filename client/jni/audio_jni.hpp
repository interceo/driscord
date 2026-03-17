#pragma once

#include <jni.h>

#include "audio/audio.hpp"
#include "audio_transport_jni.hpp"

struct AudioSenderJni {
    AudioSender        sender;
    AudioTransportJni* audio_transport; // non-owning

    explicit AudioSenderJni(AudioTransportJni* at) : audio_transport(at) {}

    bool start() {
        return sender.start([this](const uint8_t* d, size_t l) {
            audio_transport->channel.send_audio(d, l);
        });
    }
};

struct AudioReceiverJni {
    std::shared_ptr<AudioReceiver> receiver;
    explicit AudioReceiverJni(int jitter_ms)
        : receiver(std::make_shared<AudioReceiver>(jitter_ms)) {}
};
