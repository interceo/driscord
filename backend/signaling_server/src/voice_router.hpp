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

class VoiceRouter final {
public:
    using BindingSender = std::function<void(
        std::string mid, std::optional<PeerId> publisher)>;

    struct Stats {
        size_t publishers = 0;
        size_t bound_slots = 0;
        uint64_t packets_in = 0;
        uint64_t packets_out = 0;
        uint64_t bytes_out = 0;
    };

    explicit VoiceRouter(sfu::RtpFaultConfig fault_config = { },
        std::optional<boost::asio::any_io_executor> executor = { });
    ~VoiceRouter();

    VoiceRouter(const VoiceRouter&) = delete;
    VoiceRouter& operator=(const VoiceRouter&) = delete;

    void update_fault_config(sfu::RtpFaultConfig fault_config);

    void register_track(const PeerId& owner,
        std::shared_ptr<rtc::Track> track,
        BindingSender send_binding);
    void remove_peer(const PeerId& peer_id);
    [[nodiscard]] Stats stats() const;
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}
