#pragma once

#include "identity.hpp"
#include "sfu_media_utils.hpp"

#include <rtc/rtc.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace driscord {

// Owns room-wide voice fan-out. State belongs in a class here: assignments,
// NACK caches and track callbacks have a shared lifetime and are concurrently
// reached from several libdatachannel worker threads. Session and
// MediaConnections remain signaling/lifecycle coordinators only.
class VoiceRouter final {
public:
    using BindingSender = std::function<void(
        std::string mid, std::optional<PeerId> publisher)>;

    // Snapshot for /media_stats. "In" counts what publishers delivered, "out"
    // what was forwarded after fan-out, so in > 0 with out == 0 localises a
    // fault to the subscriber side.
    struct Stats {
        size_t publishers = 0;
        size_t bound_slots = 0;
        uint64_t packets_in = 0;
        uint64_t packets_out = 0;
        uint64_t bytes_out = 0;
    };

    explicit VoiceRouter(sfu::RtpFaultConfig fault_config = { });
    ~VoiceRouter();

    VoiceRouter(const VoiceRouter&) = delete;
    VoiceRouter& operator=(const VoiceRouter&) = delete;

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

} // namespace driscord
