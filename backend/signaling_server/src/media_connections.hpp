#pragma once

#include "identity.hpp"
#include "signaling_protocol.hpp"

#include <rtc/rtc.hpp>

#include <functional>
#include <memory>
#include <string>

namespace driscord {

class MediaConnections final {
public:
    using SignalSender = std::function<void(std::string)>;
    using TrackReady = std::function<void(signaling::ConnectionId,
        std::shared_ptr<rtc::Track>)>;

    MediaConnections(rtc::Configuration config,
        PeerId peer_id,
        SignalSender send_signal,
        TrackReady track_ready = { });
    ~MediaConnections();

    MediaConnections(const MediaConnections&) = delete;
    MediaConnections& operator=(const MediaConnections&) = delete;

    void accept_offer(signaling::ConnectionId connection,
        const std::string& sdp);
    void add_remote_candidate(signaling::ConnectionId connection,
        const std::string& candidate,
        const std::string& mid);
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}
