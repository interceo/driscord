#pragma once

#include "identity.hpp"
#include "sfu_media_utils.hpp"

#include <boost/asio/any_io_executor.hpp>
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

    // Snapshot for /media_stats. "In" counts what publishers delivered, "out"
    // what was forwarded after fan-out, so in > 0 with out == 0 localises a
    // fault to the subscriber side.
    struct Stats {
        size_t streaming_publishers = 0;
        size_t bound_slots = 0;
        size_t free_slots = 0;
        uint64_t video_packets_in = 0;
        uint64_t video_packets_out = 0;
        uint64_t video_bytes_out = 0;
        uint64_t audio_packets_in = 0;
        uint64_t audio_packets_out = 0;
        uint64_t audio_bytes_out = 0;
        uint64_t keyframe_requests = 0;
    };

    // The executor drives the link-model delay timer; without one an enabled
    // link model degrades to inline forwarding. Production configs are
    // zero-valued, so no timer is ever created there either way.
    explicit ScreenRouter(sfu::RtpFaultConfig fault_config = { },
        std::optional<boost::asio::any_io_executor> executor = { });
    ~ScreenRouter();

    ScreenRouter(const ScreenRouter&) = delete;
    ScreenRouter& operator=(const ScreenRouter&) = delete;

    // Swaps the whole fault stage mid-call; scenario timelines (blackout,
    // ramp) drive this through WebSocketServer::update_fault_config.
    void update_fault_config(sfu::RtpFaultConfig fault_config);

    void register_track(const PeerId& owner,
        std::shared_ptr<rtc::Track> track,
        BindingSender send_binding);
    void set_streaming(const PeerId& peer_id, bool streaming);
    void set_watching(const PeerId& peer_id,
        const PeerId& publisher_id,
        bool watching);
    void remove_peer(const PeerId& peer_id);
    [[nodiscard]] Stats stats() const;
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace driscord
