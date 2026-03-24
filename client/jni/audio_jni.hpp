#pragma once

#include <jni.h>

#include "audio/audio.hpp"

struct AudioReceiverJni {
    std::shared_ptr<AudioReceiver> receiver;
    explicit AudioReceiverJni(int jitter_ms)
        : receiver(std::make_shared<AudioReceiver>(jitter_ms)) {}
};

struct AudioSenderJni {
    AudioSender sender;

    bool start();
};
