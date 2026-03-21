#pragma once

#include <jni.h>

#include "audio_transport.hpp"
#include "transport_jni.hpp"

struct AudioTransportJni {
    AudioTransport channel;

    explicit AudioTransportJni(TransportJni& t);
};
