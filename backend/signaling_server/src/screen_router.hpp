#pragma once

#include "identity.hpp"
#include "sfu_media_utils.hpp"

#include <rtc/rtc.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace driscord {

// Owns room-wide screen fan-out. A subscriber slot is an audio/video pair with
// stable SSRCs and one publisher identity; the pairing is stateful and must
// outlive libdatachannel callbacks, hence a small RAII class is appropriate.
class ScreenRouter final {
public:
    using BindingSender = std::function<void(
        std::string mid, std::optional<PeerId> publisher)>;

    explicit ScreenRouter(sfu::RtpFaultConfig fault_config = { });
    ~ScreenRouter();

    ScreenRouter(const ScreenRouter&) = delete;
    ScreenRouter& operator=(const ScreenRouter&) = delete;

    void register_track(const PeerId& owner,
        std::shared_ptr<rtc::Track> track,
        BindingSender send_binding);
    void set_streaming(const PeerId& peer_id, bool streaming);
    void set_watching(const PeerId& peer_id,
        const PeerId& publisher_id,
        bool watching);
    void remove_peer(const PeerId& peer_id);
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace driscord
