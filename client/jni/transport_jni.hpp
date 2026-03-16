#pragma once

#include "jni_common.hpp"
#include "transport.hpp"

#include <mutex>

struct TransportJni {
    Transport   transport;
    std::mutex  cb_mutex;
    JniCallback on_peer_joined;
    JniCallback on_peer_left;

    TransportJni() {
        transport.on_peer_joined([this](const std::string& id) {
            fire_string(on_peer_joined, cb_mutex, id);
        });
        transport.on_peer_left([this](const std::string& id) {
            fire_string(on_peer_left, cb_mutex, id);
        });
    }
};
