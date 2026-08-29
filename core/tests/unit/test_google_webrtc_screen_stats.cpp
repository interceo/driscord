#include "webrtc/google_webrtc_screen_stats.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>

namespace {

using driscord::media::ScreenInboundRtpStats;
using driscord::media::ScreenSessionStats;
using driscord::media::ScreenStatsTracker;
using json = nlohmann::json;

ScreenInboundRtpStats video_stats(
    std::string mid, uint64_t packets, uint64_t bytes)
{
    return {
        .mid = std::move(mid),
        .video = true,
        .packets_received = packets,
        .bytes_received = bytes,
        .packets_lost = static_cast<int64_t>(packets / 10),
        .jitter_buffer_emitted_count = packets,
        .jitter_buffer_delay_seconds = packets * 0.02,
        .jitter_buffer_target_delay_seconds = packets * 0.03,
        .frames_decoded = static_cast<uint32_t>(packets / 2),
        .frames_dropped = static_cast<uint32_t>(packets / 20),
        .key_frames_decoded = static_cast<uint32_t>(packets / 50),
    };
}

}

TEST(GoogleWebRtcScreenStats, CoalescesPollsAndRetriesFailedRequest)
{
    ScreenStatsTracker tracker;
    tracker.set_watched("alice", true);
    tracker.set_watched("bob", true);

    const auto first = tracker.poll("alice", true);
    EXPECT_TRUE(first.start_request);
    const auto coalesced = tracker.poll("bob", true);
    EXPECT_FALSE(coalesced.start_request);
    EXPECT_EQ(first.session_generation, coalesced.session_generation);

    tracker.request_failed(first.session_generation);
    EXPECT_TRUE(tracker.poll("bob", true).start_request);
}

TEST(GoogleWebRtcScreenStats, AttributesOnlyDeltasFromCurrentBindingEpoch)
{
    ScreenStatsTracker tracker;
    tracker.set_watched("alice", true);
    tracker.set_watched("bob", true);
    tracker.set_binding("video-0", "alice");

    const auto epoch = std::chrono::steady_clock::time_point(
        std::chrono::seconds(1));
    auto request = tracker.poll("alice", true);
    tracker.consume(
        ScreenSessionStats { .inbound = { video_stats("video-0", 100, 10'000) } },
        request.session_generation, epoch);
    EXPECT_EQ(json::parse(tracker.poll("alice", false).json)["video"]
                                                            ["packetsReceived"],
        0);

    request = tracker.poll("alice", true);
    tracker.consume(
        ScreenSessionStats { .inbound = { video_stats("video-0", 130, 16'000) } },
        request.session_generation, epoch + std::chrono::seconds(1));
    const auto alice = json::parse(tracker.poll("alice", false).json);
    EXPECT_EQ(alice["video"]["packetsReceived"], 30);
    EXPECT_EQ(alice["video"]["bytesReceived"], 6'000);
    EXPECT_EQ(alice["measuredKbps"], 48);

    tracker.set_binding("video-0", "bob");
    request = tracker.poll("bob", true);
    tracker.consume(
        ScreenSessionStats { .inbound = { video_stats("video-0", 150, 20'000) } },
        request.session_generation, epoch + std::chrono::seconds(2));
    EXPECT_EQ(json::parse(tracker.poll("bob", false).json)["video"]
                                                          ["packetsReceived"],
        0);

    request = tracker.poll("bob", true);
    tracker.consume(
        ScreenSessionStats { .inbound = { video_stats("video-0", 160, 22'000) } },
        request.session_generation, epoch + std::chrono::seconds(3));
    EXPECT_EQ(json::parse(tracker.poll("bob", false).json)["video"]
                                                          ["packetsReceived"],
        10);
}

TEST(GoogleWebRtcScreenStats, RejectsPreviousSessionCallback)
{
    ScreenStatsTracker tracker;
    tracker.set_watched("alice", true);
    tracker.set_binding("video-0", "alice");
    const auto stale = tracker.poll("alice", true);

    tracker.reset_session();
    tracker.consume(
        ScreenSessionStats { .inbound = { video_stats("video-0", 100, 10'000) } },
        stale.session_generation);

    const auto current = tracker.poll("alice", true);
    EXPECT_TRUE(current.start_request);
    EXPECT_NE(current.session_generation, stale.session_generation);
    EXPECT_TRUE(json::parse(current.json)["video"].empty());
}
