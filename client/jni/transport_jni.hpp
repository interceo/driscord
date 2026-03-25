#pragma once

#include "jni_common.hpp"
#include "transport.hpp"

#include <mutex>

struct TransportJni {
    std::mutex  cb_mutex;
    JniCallback on_peer_joined;
    JniCallback on_peer_left;

    explicit TransportJni(Transport& transport);
};
