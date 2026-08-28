#include "headless_audio.hpp"
#include "media_metrics.hpp"
#include "net_scenarios.hpp"
#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Reference-free degradation ladder for screen video: one publisher pushes a
// synthetic 320x180@30fps pattern to one watcher at increasing
// Gilbert–Elliott loss. Monotonicity across the rungs is the primary gate;
// the clean rung carries the absolute freeze tripwire.

namespace {

using test_util::EventCollector;
using test_util::SignalingServerFixture;
using test_util::Waiter;

class DecodedFrameCounter {
public:
    void observe(const driscord::media::DecodedVideoFrameView& frame)
    {
        if (frame.width <= 0 || frame.height <= 0 || frame.y == nullptr) {
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            ++frames_;
        }
        changed_.notify_all();
    }

    [[nodiscard]] size_t frames() const
    {
        std::scoped_lock lock(mutex_);
        return frames_;
    }

    bool reached(size_t minimum) const
    {
        std::scoped_lock lock(mutex_);
        return frames_ >= minimum;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    size_t frames_ = 0;
};

std::vector<uint8_t> make_bgra_frame(
    int width, int height, uint8_t seed, size_t frame_index)
{
    std::vector<uint8_t> result(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset
                = (static_cast<size_t>(y) * static_cast<size_t>(width)
                      + static_cast<size_t>(x))
                * 4;
            result[offset] = static_cast<uint8_t>(
                seed + (x + static_cast<int>(frame_index)) % 97);
            result[offset + 1] = static_cast<uint8_t>(
                seed / 2 + (y + static_cast<int>(frame_index)) % 83);
            result[offset + 2] = static_cast<uint8_t>(
                220 - seed / 2 + (x / 8 + y / 8) % 31);
            result[offset + 3] = 255;
        }
    }
    return result;
}

struct ScreenRungMetrics {
    size_t decoded_frames = 0;
    uint32_t frames_decoded = 0;
    uint32_t frames_dropped = 0;
    uint32_t freeze_count = 0;
    double total_freezes_duration_seconds = 0.0;
    uint32_t pli_count = 0;
    int64_t packets_lost = 0;
    uint32_t frames_received = 0;
    double estimated_playout_timestamp_ms = -1.0;
    uint32_t publisher_frames_encoded = 0;
    size_t errors = 0;
};

ScreenRungMetrics run_screen_rung(driscord::sfu::RtpFaultConfig faults,
    size_t decoded_frame_floor,
    size_t max_feed_frames)
{
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcScreenSession;
    using driscord::media::ScreenConnectionState;
    using driscord::media::ScreenSessionCallbacks;
    using driscord::media::ScreenSessionConfig;
    using driscord::media::ScreenSessionStats;

    SignalingServerFixture server { std::move(faults) };

    const driscord::media::GoogleWebRtcRuntimeConfig runtime_config {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
    GoogleWebRtcRuntime publisher_runtime(runtime_config);
    GoogleWebRtcRuntime listener_runtime(runtime_config);
    Transport publisher_transport;
    Transport listener_transport;
    Waiter publisher_connected;
    Waiter listener_connected;
    EventCollector<std::string> streaming_publishers;
    EventCollector<std::pair<std::string, std::optional<std::string>>> bindings;
    EventCollector<std::string> errors;
    DecodedFrameCounter decoded;
    ScreenRungMetrics metrics;

    auto make_screen = [&errors](GoogleWebRtcRuntime& runtime,
                           Transport& transport,
                           Waiter& connected,
                           bool sharing,
                           DecodedFrameCounter* probe) {
        ScreenSessionCallbacks callbacks;
        callbacks.on_offer = [&transport](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Screen, sdp);
        };
        callbacks.on_candidate =
            [&transport](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Screen,
                    candidate, mid);
            };
        callbacks.on_state = [&connected](ScreenConnectionState state) {
            if (state == ScreenConnectionState::Connected) {
                connected.signal();
            }
        };
        callbacks.on_error = [&errors](std::string message) {
            errors.push(std::move(message));
        };
        if (probe) {
            callbacks.on_remote_video =
                [probe](std::string_view,
                    driscord::media::DecodedVideoFrameView frame) {
                    probe->observe(frame);
                };
        }
        return std::make_unique<GoogleWebRtcScreenSession>(runtime,
            ScreenSessionConfig {
                .remote_stream_slots = 2,
                .sharing_enabled = sharing,
                .system_audio_enabled = false,
                .max_video_bitrate_bps = 1'500'000,
            },
            std::move(callbacks));
    };

    auto publisher_screen = make_screen(publisher_runtime, publisher_transport,
        publisher_connected, true, nullptr);
    auto listener_screen = make_screen(listener_runtime, listener_transport,
        listener_connected, false, &decoded);

    auto connect_screen = [](Transport& transport,
                              GoogleWebRtcScreenSession& screen) {
        transport.on_media_answer(
            [&screen](signaling::ConnectionId connection,
                const std::string& sdp) {
                if (connection == signaling::ConnectionId::Screen) {
                    screen.apply_answer(sdp);
                }
            });
        transport.on_media_candidate(
            [&screen](signaling::ConnectionId connection,
                const std::string& candidate,
                const std::string& mid) {
                if (connection == signaling::ConnectionId::Screen) {
                    screen.add_remote_candidate(candidate, mid);
                }
            });
    };
    connect_screen(publisher_transport, *publisher_screen);
    connect_screen(listener_transport, *listener_screen);
    listener_transport.on_streaming_started(
        [&streaming_publishers](const std::string& peer_id) {
            streaming_publishers.push(peer_id);
        });
    listener_transport.on_media_track_binding(
        [&bindings](signaling::ConnectionId connection,
            const std::string& mid,
            const std::optional<driscord::PeerId>& peer_id) {
            if (connection == signaling::ConnectionId::Screen) {
                bindings.push({ mid,
                    peer_id ? std::optional<std::string>(peer_id->value)
                            : std::optional<std::string>() });
            }
        });

    EXPECT_TRUE(publisher_transport.connect(server.ws_url()));
    EXPECT_TRUE(listener_transport.connect(server.ws_url()));
    EXPECT_TRUE(test_util::wait_for_local_id(publisher_transport));
    EXPECT_TRUE(test_util::wait_for_local_id(listener_transport));

    EXPECT_TRUE(publisher_screen->start());
    EXPECT_TRUE(listener_screen->start());
    EXPECT_TRUE(publisher_connected.wait_for());
    EXPECT_TRUE(listener_connected.wait_for());

    publisher_transport.send_streaming_start();
    EXPECT_TRUE(streaming_publishers.wait_for_count(1));
    listener_transport.send_watch_start(publisher_transport.local_id());
    EXPECT_TRUE(bindings.wait_for_count(2));

    constexpr int kWidth = 320;
    constexpr int kHeight = 180;
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t start_us
        = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
    // Feed until the watcher clears the floor: the encoder legitimately drops
    // frames while ramping, so a fixed submission count would gate on encoder
    // efficiency. The cap only bounds the failure path.
    for (size_t frame = 0;
        frame < max_feed_frames
        && (frame < 30 || !decoded.reached(decoded_frame_floor));
        ++frame) {
        const auto bgra = make_bgra_frame(kWidth, kHeight, 31, frame);
        const int64_t timestamp_us
            = start_us + static_cast<int64_t>(frame) * 33'333;
        (void)publisher_screen->submit_bgra_frame(bgra, kWidth, kHeight,
            kWidth * 4, timestamp_us);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    EXPECT_GE(decoded.frames(), decoded_frame_floor)
        << "decoded frame goodput stayed under the floor";

    const auto listener_stats
        = test_util::sample_stats<ScreenSessionStats>(*listener_screen, 1);
    const auto publisher_stats
        = test_util::sample_stats<ScreenSessionStats>(*publisher_screen, 1);
    EXPECT_EQ(listener_stats.size(), 1u);
    EXPECT_EQ(publisher_stats.size(), 1u);
    if (!listener_stats.empty()) {
        for (const auto& inbound : listener_stats.front().inbound) {
            if (!inbound.video) {
                continue;
            }
            metrics.frames_decoded = inbound.frames_decoded;
            metrics.frames_dropped = inbound.frames_dropped;
            metrics.freeze_count = inbound.video_playback.freeze_count;
            metrics.total_freezes_duration_seconds
                = inbound.video_playback.total_freezes_duration_seconds;
            metrics.pli_count = inbound.video_playback.pli_count;
            metrics.packets_lost = inbound.packets_lost;
            metrics.frames_received = inbound.video_playback.frames_received;
            metrics.estimated_playout_timestamp_ms
                = inbound.rtp.estimated_playout_timestamp_ms;
        }
    }
    if (!publisher_stats.empty()) {
        metrics.publisher_frames_encoded
            = publisher_stats.front().video_frames_encoded;
    }
    metrics.decoded_frames = decoded.frames();
    metrics.errors = errors.snapshot().size();

    publisher_screen->close();
    listener_screen->close();
    publisher_transport.disconnect();
    listener_transport.disconnect();
    return metrics;
}

} // namespace

TEST(GoogleWebRtcDegradedScreen, LossLadderDegradesMonotonically)
{
    // Deterministic Gilbert–Elliott rungs, mean burst of 4 packets, fixed
    // seed. Each rung is a fresh in-process SFU. Video packets are larger and
    // more numerous than voice, so the same mean loss bites harder.
    const auto clean = run_screen_rung(driscord::sfu::RtpFaultConfig { },
        /*decoded_frame_floor=*/90, /*max_feed_frames=*/300);
    const auto light = run_screen_rung(
        driscord::sfu::RtpFaultConfig {
            .burst = test_util::burst_loss(0.02, 4.0, 23),
        },
        90, 300);
    const auto heavy = run_screen_rung(
        driscord::sfu::RtpFaultConfig {
            .burst = test_util::burst_loss(0.08, 4.0, 23),
        },
        60, 450);

    EXPECT_EQ(clean.errors, 0u);
    EXPECT_EQ(light.errors, 0u);
    EXPECT_EQ(heavy.errors, 0u);

    // Decode kept making progress on every rung: NACK/PLI recovery works
    // under bursty loss instead of stalling the decoder.
    EXPECT_GT(clean.frames_decoded, 0u);
    EXPECT_GT(light.frames_decoded, 0u);
    EXPECT_GT(heavy.frames_decoded, 0u);

    // New stats fields carry real data.
    EXPECT_GT(clean.frames_received, 0u);
    EXPECT_GT(clean.estimated_playout_timestamp_ms, 0.0);
    EXPECT_GT(clean.publisher_frames_encoded, 0u);

    // Monotonicity: more injected loss must never lose fewer packets or
    // trigger fewer recovery requests.
    EXPECT_LE(std::max<int64_t>(clean.packets_lost, 0),
        std::max<int64_t>(heavy.packets_lost, 0));
    EXPECT_LE(clean.pli_count, heavy.pli_count);
    EXPECT_LE(clean.total_freezes_duration_seconds,
        heavy.total_freezes_duration_seconds + 1e-9);
    EXPECT_GT(heavy.packets_lost, 0);

    // Absolute tripwire on the clean rung only: an unimpaired loopback link
    // must not freeze.
    EXPECT_TRUE(test_util::gate_le("screen.clean.freeze_count",
        clean.freeze_count, 0.0));
    EXPECT_TRUE(test_util::gate_le("screen.clean.frames_dropped_ratio",
        clean.frames_decoded == 0
            ? 0.0
            : static_cast<double>(clean.frames_dropped)
                / static_cast<double>(clean.frames_decoded),
        0.1));
}
