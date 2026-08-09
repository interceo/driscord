#pragma once

#include "webrtc/google_webrtc_runtime.hpp"

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "rtc_base/thread.h"

#include <cstdint>
#include <memory>
#include <span>

namespace driscord::media {

class InjectedPcmSource;

// Shared by sessions so the factory and its threads cannot disappear while a
// PeerConnection is still alive. This header is private to the backend target.
struct GoogleWebRtcRuntime::Impl {
    std::unique_ptr<webrtc::Thread> network_thread;
    std::unique_ptr<webrtc::Thread> worker_thread;
    std::unique_ptr<webrtc::Thread> signaling_thread;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    std::shared_ptr<InjectedPcmSource> injected_pcm_source;

    explicit Impl(const GoogleWebRtcRuntimeConfig& config);
    ~Impl();

    [[nodiscard]] bool submit_recorded_audio_10ms(
        std::span<const int16_t> samples);
};

} // namespace driscord::media
