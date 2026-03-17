#pragma once

#include <jni.h>
#include <memory>

#include "audio/audio_receiver.hpp"

struct AudioReceiverJni {
    std::shared_ptr<AudioReceiver> receiver;
    explicit AudioReceiverJni(int jitter_ms)
        : receiver(std::make_shared<AudioReceiver>(jitter_ms)) {}
};
