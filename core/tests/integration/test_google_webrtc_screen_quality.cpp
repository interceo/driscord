#include "frame_marker.hpp"
#include "headless_audio.hpp"
#include "media_metrics.hpp"
#include "rtc_cleanup_env.hpp"
#include "screen_content.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "video_quality.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Full-reference screen quality on the clean production path: marked source
// frames, PSNR/SSIM against the exact submitted reference. Freeze and
// framerate continuity come from libwebrtc's own VideoReceiveStats (the W3C
// definitions), not from test-side math. The first decoded frames are
// excluded from the reference comparison — the encoder's rate ramp is
// startup behavior, not steady quality. With DRISCORD_MEDIA_DUMP_DIR set,
// every compared pair is also written as aligned Y4M for the ffmpeg
// cross-check (scripts/media_metrics_crosscheck.sh).

namespace {

using test_util::EventCollector;
using test_util::SignalingServerFixture;
using test_util::Waiter;

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr size_t kWarmupFrames = 30;

using ContentGenerator
    = std::function<std::vector<uint8_t>(int, int, size_t)>;

struct QualityRun {
    test_util::VideoQualityReport report;
    // Video playback continuity as libwebrtc itself measured it.
    std::optional<driscord::media::VideoReceiveStats> video_stats;
    size_t decoded_frames = 0;
    size_t errors = 0;
};

QualityRun run_screen_quality_call(const ContentGenerator& content,
    size_t max_frames,
    size_t decoded_target,
    const char* dump_label)
{
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcScreenSession;
    using driscord::media::ScreenConnectionState;
    using driscord::media::ScreenSessionCallbacks;
    using driscord::media::ScreenSessionConfig;

    SignalingServerFixture server;

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

    test_util::ReferenceFrameStore references(240);
    std::mutex quality_mutex;
    test_util::VideoQualityAccumulator accumulator(references, kWidth,
        kHeight);
    if (const auto dump_dir = test_util::media_dump_dir()) {
        accumulator.enable_dump(*dump_dir, dump_label);
    }
    std::atomic<size_t> decoded_count { 0 };
    Waiter decoded_target_reached;

    ScreenSessionCallbacks publisher_callbacks;
    publisher_callbacks.on_offer = [&](std::string sdp) {
        publisher_transport.send_media_offer(
            signaling::ConnectionId::Screen, sdp);
    };
    publisher_callbacks.on_candidate = [&](std::string candidate,
                                           std::string mid) {
        publisher_transport.send_media_candidate(
            signaling::ConnectionId::Screen, candidate, mid);
    };
    publisher_callbacks.on_state = [&](ScreenConnectionState state) {
        if (state == ScreenConnectionState::Connected) {
            publisher_connected.signal();
        }
    };
    publisher_callbacks.on_error = [&errors](std::string message) {
        errors.push(std::move(message));
    };
    auto publisher_screen = std::make_unique<GoogleWebRtcScreenSession>(
        publisher_runtime,
        ScreenSessionConfig {
            .remote_stream_slots = 2,
            .sharing_enabled = true,
            .system_audio_enabled = false,
            .max_video_bitrate_bps = 1'500'000,
        },
        std::move(publisher_callbacks));

    ScreenSessionCallbacks listener_callbacks;
    listener_callbacks.on_offer = [&](std::string sdp) {
        listener_transport.send_media_offer(
            signaling::ConnectionId::Screen, sdp);
    };
    listener_callbacks.on_candidate = [&](std::string candidate,
                                          std::string mid) {
        listener_transport.send_media_candidate(
            signaling::ConnectionId::Screen, candidate, mid);
    };
    listener_callbacks.on_state = [&](ScreenConnectionState state) {
        if (state == ScreenConnectionState::Connected) {
            listener_connected.signal();
        }
    };
    listener_callbacks.on_error = [&errors](std::string message) {
        errors.push(std::move(message));
    };
    listener_callbacks.on_remote_video
        = [&](std::string_view, driscord::media::DecodedVideoFrameView frame) {
              const size_t count = decoded_count.fetch_add(1) + 1;
              if (count <= kWarmupFrames) {
                  return;
              }
              {
                  std::scoped_lock lock(quality_mutex);
                  accumulator.on_decoded(frame,
                      std::chrono::steady_clock::now());
              }
              if (count == decoded_target) {
                  decoded_target_reached.signal();
              }
          };
    auto listener_screen = std::make_unique<GoogleWebRtcScreenSession>(
        listener_runtime,
        ScreenSessionConfig {
            .remote_stream_slots = 2,
            .sharing_enabled = false,
            .system_audio_enabled = false,
            .max_video_bitrate_bps = 1'500'000,
        },
        std::move(listener_callbacks));

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

    QualityRun result;
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

    const auto base = std::chrono::steady_clock::now();
    for (size_t frame = 0; frame < max_frames; ++frame) {
        if (decoded_target_reached.fired()) {
            break;
        }
        auto bgra = content(kWidth, kHeight, frame);
        test_util::encode_frame_marker(bgra, kWidth, kHeight, kWidth * 4,
            static_cast<uint32_t>(frame));
        const auto capture_time = std::chrono::steady_clock::now();
        {
            std::scoped_lock lock(quality_mutex);
            references.add(static_cast<uint32_t>(frame),
                test_util::bgra_to_i420(bgra, kWidth, kHeight, kWidth * 4),
                capture_time);
        }
        const int64_t timestamp_us
            = std::chrono::duration_cast<std::chrono::microseconds>(
                base.time_since_epoch())
                  .count()
            + static_cast<int64_t>(frame) * 33'333;
        (void)publisher_screen->submit_bgra_frame(bgra, kWidth, kHeight,
            kWidth * 4, timestamp_us);
        std::this_thread::sleep_until(
            base + std::chrono::microseconds((frame + 1) * 33'333));
    }
    EXPECT_TRUE(decoded_target_reached.wait_for(std::chrono::seconds(5)))
        << "decoded only " << decoded_count.load() << "/" << decoded_target;

    // Playback continuity from the receiver's own inbound-rtp stats — the
    // production measurement the freeze/framerate gates sit on.
    const auto stats_samples
        = test_util::sample_stats<driscord::media::ScreenSessionStats>(
            *listener_screen, 1);
    if (!stats_samples.empty()) {
        for (const auto& inbound : stats_samples.front().inbound) {
            if (inbound.video && inbound.packets_received > 0) {
                result.video_stats = inbound.video_playback;
            }
        }
    }

    {
        std::scoped_lock lock(quality_mutex);
        result.report = accumulator.report();
    }
    result.decoded_frames = decoded_count.load();
    result.errors = errors.snapshot().size();

    publisher_screen->close();
    listener_screen->close();
    publisher_transport.disconnect();
    listener_transport.disconnect();
    return result;
}

} // namespace

TEST(GoogleWebRtcScreenQuality, CleanScrollingTextPsnrSsim)
{
    const auto run = run_screen_quality_call(
        [](int width, int height, size_t index) {
            return test_util::scrolling_text_frame(width, height, index);
        },
        /*max_frames=*/450, /*decoded_target=*/330,
        "screen_quality_scrolling_text");

    EXPECT_EQ(run.errors, 0u);
    ASSERT_GT(run.report.compared_frames, 200u);

    // Marker integrity is a prerequisite for every full-reference number.
    const double marker_failure_ratio
        = static_cast<double>(run.report.undecodable_markers)
        / static_cast<double>(
            run.report.compared_frames + run.report.undecodable_markers);
    EXPECT_TRUE(test_util::gate_le("screen_quality.marker_failure_ratio",
        marker_failure_ratio, 0.005));

    EXPECT_TRUE(test_util::gate_ge("screen_quality.ssim_mean",
        run.report.ssim_mean, 0.95));
    EXPECT_TRUE(test_util::gate_ge("screen_quality.psnr_mean",
        run.report.psnr_mean, 36.0));
    EXPECT_TRUE(test_util::gate_ge("screen_quality.psnr_min",
        run.report.psnr_min, 28.0));
    const double dropped_ratio = run.report.compared_frames == 0
        ? 0.0
        : static_cast<double>(run.report.dropped_frames)
            / static_cast<double>(
                run.report.compared_frames + run.report.dropped_frames);
    EXPECT_TRUE(test_util::gate_le("screen_quality.dropped_ratio",
        dropped_ratio, 0.02));
    ASSERT_TRUE(run.video_stats.has_value());
    EXPECT_TRUE(test_util::gate_le("screen_quality.freeze_count",
        static_cast<double>(run.video_stats->freeze_count), 0.0));

    // Glass-to-glass delay: observe-only until a baseline exists — the JSON
    // lines feed the trend pipeline either way.
    if (!run.report.e2e_delay_ms.empty()) {
        (void)test_util::gate_le("screen_quality.e2e_delay_p95_ms",
            test_util::percentile(run.report.e2e_delay_ms, 95.0), 400.0);
    }
}

TEST(GoogleWebRtcScreenQuality, SlideTransitionsKeepHarmonicFramerate)
{
    const auto run = run_screen_quality_call(
        [](int width, int height, size_t index) {
            return test_util::sliding_blocks_frame(width, height, index);
        },
        /*max_frames=*/330, /*decoded_target=*/240,
        "screen_quality_sliding_blocks");

    EXPECT_EQ(run.errors, 0u);
    ASSERT_GT(run.report.compared_frames, 150u);
    // Harmonic framerate penalizes stalls: 30 fps input must not degrade
    // into a slideshow even while slides transition. Both numbers come from
    // libwebrtc's own inter-frame accounting.
    ASSERT_TRUE(run.video_stats.has_value());
    EXPECT_TRUE(test_util::gate_ge("screen_quality.harmonic_framerate",
        test_util::harmonic_framerate(*run.video_stats), 24.0));
    EXPECT_TRUE(test_util::gate_le("screen_quality.slides_freeze_count",
        static_cast<double>(run.video_stats->freeze_count), 0.0));
}
