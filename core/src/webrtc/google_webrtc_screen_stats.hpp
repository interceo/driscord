#pragma once

#include "webrtc/google_webrtc_screen_session.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace driscord::media {

class ScreenStatsTracker final {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    struct Poll {
        std::string json;
        bool start_request = false;
        uint64_t session_generation = 0;
    };

    void set_binding(
        std::string mid, std::optional<std::string> peer_id);
    void set_watched(std::string peer_id, bool watched);
    void clear_watched();
    void remove_peer(std::string_view peer_id);

    void reset_session();

    [[nodiscard]] Poll poll(
        std::string_view peer_id, bool session_available);
    void request_failed(uint64_t session_generation);
    void consume(ScreenSessionStats stats,
        uint64_t session_generation,
        TimePoint now = std::chrono::steady_clock::now());

private:
    struct Cache {
        std::string json
            = R"({"video":{},"audio":{},"outbound":{},"measuredKbps":0})";
        uint64_t received_bytes = 0;
        TimePoint updated_at;
    };

    void update_peer(std::string_view peer_id,
        const ScreenSessionStats& stats,
        TimePoint now,
        Cache& cache) const;

    std::unordered_map<std::string, std::string> bindings_;
    std::unordered_set<std::string> watched_peers_;
    std::unordered_map<std::string, Cache> cache_by_peer_;
    std::unordered_map<std::string, ScreenInboundRtpStats> baselines_;
    bool request_in_flight_ = false;
    uint64_t session_generation_ = 0;
};

}
