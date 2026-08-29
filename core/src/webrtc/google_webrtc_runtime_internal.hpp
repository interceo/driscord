#pragma once

#include "webrtc/google_webrtc_runtime.hpp"

#include "api/audio/audio_device.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "rtc_base/thread.h"

#include <cstdint>
#include <memory>
#include <span>

namespace driscord::media {

class InjectedPcmSource;

struct GoogleWebRtcRuntime::Impl {
    std::unique_ptr<webrtc::Thread> network_thread;
    std::unique_ptr<webrtc::Thread> worker_thread;
    std::unique_ptr<webrtc::Thread> signaling_thread;
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    std::shared_ptr<InjectedPcmSource> injected_pcm_source;
    bool audio_device_degraded = false;
    std::vector<IceServerConfig> ice_servers;

    explicit Impl(const GoogleWebRtcRuntimeConfig& config);
    ~Impl();

    [[nodiscard]] bool submit_recorded_audio_10ms(
        std::span<const int16_t> samples);
    [[nodiscard]] std::vector<AudioDeviceInfo> audio_devices(
        bool recording) const;
    [[nodiscard]] bool set_audio_device(
        bool recording, std::string_view id);
};

}
