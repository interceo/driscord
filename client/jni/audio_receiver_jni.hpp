#pragma once

#include <jni.h>

#include "audio/audio_receiver.hpp"

struct AudioReceiverJni {
    AudioReceiver receiver;
    explicit AudioReceiverJni(int jitter_ms) : receiver(jitter_ms) {}
};
