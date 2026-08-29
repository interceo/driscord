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

class ScreenRouter final {
public:
    using BindingSender = std::function<void(
        std::string mid, std::optional<PeerId> publisher)>;

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

    explicit ScreenRouter(sfu::RtpFaultConfig fault_config = { },
        std::optional<boost::asio::any_io_executor> executor = { });
    ~ScreenRouter();

    ScreenRouter(const ScreenRouter&) = delete;
    ScreenRouter& operator=(const ScreenRouter&) = delete;

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

}
