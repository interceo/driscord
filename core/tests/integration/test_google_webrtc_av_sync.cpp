#include "audio_probe.hpp"
#include "av_sync.hpp"
#include "frame_marker.hpp"
#include "headless_audio.hpp"
#include "media_dump.hpp"
#include "media_metrics.hpp"
#include "net_scenarios.hpp"
#include "rtc_cleanup_env.hpp"
#include "screen_content.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using test_util::EventCollector;
using test_util::SignalingServerFixture;
using test_util::Waiter;

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr auto kFramePeriod = std::chrono::microseconds(33'333);
constexpr size_t kFirstEventFrame = 60;
constexpr size_t kEventStrideFrames = 30;
constexpr size_t kEventCount = 18;
constexpr size_t kTotalVideoFrames
    = kFirstEventFrame + kEventStrideFrames * kEventCount + 60;

struct AvSyncRun {
    test_util::AvSyncReport markers;
    std::vector<double> stats_skew_ms;
    bool audio_playout_ts_seen = false;
    bool video_playout_ts_seen = false;
    size_t decoded_frames = 0;
    size_t errors = 0;
};

AvSyncRun run_av_sync_call(driscord::sfu::RtpFaultConfig faults,
    const char* dump_label,
    double detector_threshold = 0.35)
{
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcScreenSession;
    using driscord::media::ScreenConnectionState;
    using driscord::media::ScreenSessionCallbacks;
    using driscord::media::ScreenSessionConfig;
    using driscord::media::ScreenSessionStats;

    SignalingServerFixture server { std::move(faults) };

    test_util::AvSyncCorrelator correlator;
    test_util::ChirpDetector detector(
        test_util::make_chirp(48'000, 20, 1'000.0, 4'000.0, 16'000),
        48'000, detector_threshold);
    std::mutex probe_mutex;

    const auto dump_dir = test_util::media_dump_dir();
    test_util::WavWriter reference_audio_dump;
    test_util::WavWriter rendered_audio_dump;
    if (dump_dir) {
        (void)reference_audio_dump.open(
            *dump_dir / (std::string(dump_label) + ".ref.wav"), 48'000, 1);
    }

    driscord::media::InjectedAudioDeviceConfig listener_audio;
    listener_audio.on_rendered_audio
        = [&](std::span<const int16_t> samples, int sample_rate_hz,
              size_t channels) {
              std::scoped_lock lock(probe_mutex);
              if (dump_dir && !rendered_audio_dump.is_open()) {
                  (void)rendered_audio_dump.open(
                      *dump_dir
                          / (std::string(dump_label) + ".rendered.wav"),
                      sample_rate_hz, static_cast<uint16_t>(channels));
              }
              (void)rendered_audio_dump.write(samples);
              detector.push(samples, channels,
                  std::chrono::steady_clock::now());
          };
    const driscord::media::GoogleWebRtcRuntimeConfig publisher_config {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
    const driscord::media::GoogleWebRtcRuntimeConfig listener_config {
        .injected_audio_device = listener_audio,
    };
    GoogleWebRtcRuntime publisher_runtime(publisher_config);
    GoogleWebRtcRuntime listener_runtime(listener_config);
    Transport publisher_transport;
    Transport listener_transport;
    Waiter publisher_connected;
    Waiter listener_connected;
    EventCollector<std::string> streaming_publishers;
    EventCollector<std::pair<std::string, std::optional<std::string>>> bindings;
    EventCollector<std::string> errors;
    std::atomic<size_t> decoded_frames { 0 };
    std::optional<std::chrono::steady_clock::time_point> media_base;
    std::mutex base_mutex;

    auto capture_time_of_frame
        = [&](uint32_t index) -> std::optional<std::chrono::steady_clock::time_point> {
        std::scoped_lock lock(base_mutex);
        if (!media_base) {
            return std::nullopt;
        }
        return *media_base + index * kFramePeriod;
    };

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
            .system_audio_enabled = true,
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
              decoded_frames.fetch_add(1);
              const auto now = std::chrono::steady_clock::now();
              const auto marker = test_util::decode_frame_marker(frame,
                  kWidth, kHeight);
              if (!marker) {
                  return;
              }
              const auto capture = capture_time_of_frame(marker->frame_index);
              if (!capture) {
                  return;
              }
              std::scoped_lock lock(probe_mutex);
              correlator.on_video_frame(marker->frame_index, *capture, now);
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

    AvSyncRun result;
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

    const auto base = std::chrono::steady_clock::now() + 100ms;
    {
        std::scoped_lock lock(base_mutex);
        media_base = base;
    }
    for (size_t k = 0; k < kEventCount; ++k) {
        const auto frame_index = static_cast<uint32_t>(
            kFirstEventFrame + k * kEventStrideFrames);
        correlator.register_event(frame_index,
            base + frame_index * kFramePeriod);
    }

    std::thread video_feeder([&] {
        for (size_t frame = 0; frame < kTotalVideoFrames; ++frame) {
            std::this_thread::sleep_until(base + frame * kFramePeriod);
            auto bgra = test_util::static_terminal_frame(kWidth, kHeight,
                frame);
            test_util::encode_frame_marker(bgra, kWidth, kHeight, kWidth * 4,
                static_cast<uint32_t>(frame));
            const int64_t timestamp_us
                = std::chrono::duration_cast<std::chrono::microseconds>(
                    (base + frame * kFramePeriod).time_since_epoch())
                      .count();
            (void)publisher_screen->submit_bgra_frame(bgra, kWidth, kHeight,
                kWidth * 4, timestamp_us);
        }
    });

    std::thread audio_feeder([&] {
        const auto chirp = test_util::make_chirp(48'000, 20, 1'000.0, 4'000.0,
            16'000);
        constexpr double kPi = 3.14159265358979323846;
        constexpr size_t kSamplesPerFrame = 480;
        const size_t total_audio_frames
            = kTotalVideoFrames * 33'333 / 10'000 + 50;
        std::vector<size_t> chirp_starts;
        for (size_t k = 0; k < kEventCount; ++k) {
            const size_t frame_index
                = kFirstEventFrame + k * kEventStrideFrames;
            chirp_starts.push_back(static_cast<size_t>(
                static_cast<uint64_t>(frame_index) * 33'333 * 48 / 1'000));
        }
        for (size_t frame = 0; frame < total_audio_frames; ++frame) {
            std::this_thread::sleep_until(
                base + frame * std::chrono::milliseconds(10));
            std::vector<int16_t> samples(kSamplesPerFrame);
            const size_t first_sample = frame * kSamplesPerFrame;
            for (size_t i = 0; i < kSamplesPerFrame; ++i) {
                samples[i] = static_cast<int16_t>(std::lround(
                    6'000.0 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(first_sample + i) / 48'000.0)));
            }
            for (const size_t start : chirp_starts) {
                if (start < first_sample + kSamplesPerFrame
                    && start + chirp.size() > first_sample) {
                    const size_t chirp_from
                        = start > first_sample ? 0 : first_sample - start;
                    const size_t frame_from
                        = start > first_sample ? start - first_sample : 0;
                    test_util::mix_chirp(
                        std::span<int16_t>(samples).subspan(frame_from),
                        std::span<const int16_t>(chirp).subspan(chirp_from),
                        0);
                }
            }
            (void)reference_audio_dump.write(samples);
            (void)publisher_runtime.submit_recorded_audio_10ms(samples);
        }
    });

    const size_t stats_polls = kTotalVideoFrames * 33 / 1'000;
    for (size_t poll = 0; poll < stats_polls; ++poll) {
        std::this_thread::sleep_for(1s);
        const auto samples = test_util::sample_stats<ScreenSessionStats>(
            *listener_screen, 1);
        if (samples.empty()) {
            continue;
        }
        const driscord::media::ScreenInboundRtpStats* audio_row = nullptr;
        const driscord::media::ScreenInboundRtpStats* video_row = nullptr;
        for (const auto& inbound : samples.front().inbound) {
            if (inbound.video && inbound.packets_received > 0) {
                video_row = &inbound;
            }
            if (!inbound.video && inbound.packets_received > 0) {
                audio_row = &inbound;
            }
        }
        if (video_row != nullptr
            && video_row->rtp.estimated_playout_timestamp_ms >= 0.0) {
            result.video_playout_ts_seen = true;
        }
        if (audio_row != nullptr
            && audio_row->rtp.estimated_playout_timestamp_ms >= 0.0) {
            result.audio_playout_ts_seen = true;
        }
        if (audio_row != nullptr && video_row != nullptr) {
            const auto skew
                = test_util::playout_skew_ms(audio_row->rtp, video_row->rtp);
            if (skew) {
                result.stats_skew_ms.push_back(*skew);
            }
        }
    }

    video_feeder.join();
    audio_feeder.join();
    std::this_thread::sleep_for(500ms);

    {
        std::scoped_lock lock(probe_mutex);
        for (const auto detection : detector.detections()) {
            correlator.on_chirp(detection);
        }
        result.markers = correlator.report();
    }
    result.decoded_frames = decoded_frames.load();
    result.errors = errors.snapshot().size();

    publisher_screen->close();
    listener_screen->close();
    publisher_transport.disconnect();
    listener_transport.disconnect();
    return result;
}

void expect_sync_within(const AvSyncRun& run,
    const char* label,
    double p95_limit_ms,
    size_t min_matched = kEventCount * 3 / 4)
{
    EXPECT_EQ(run.errors, 0u);
    EXPECT_GT(run.decoded_frames, 100u);
    ASSERT_GE(run.markers.matched, min_matched)
        << "matched only " << run.markers.matched << "/" << kEventCount
        << " sync events";
    EXPECT_TRUE(test_util::gate_le(
        std::string(label) + ".av_sync_abs_p95_ms", run.markers.abs_p95_ms,
        p95_limit_ms));
    EXPECT_TRUE(test_util::gate_le(
        std::string(label) + ".av_sync_abs_max_ms", run.markers.abs_max_ms,
        125.0));
    EXPECT_TRUE(test_util::gate_le(
        std::string(label) + ".av_sync_audio_lead_max_ms",
        run.markers.audio_lead_max_ms, 45.0));
    (void)test_util::gate_le(std::string(label) + ".av_sync_abs_p50_ms",
        run.markers.abs_p50_ms, 25.0);
    (void)test_util::gate_le(std::string(label) + ".av_sync_abs_p95_ideal_ms",
        run.markers.abs_p95_ms, 45.0);
}

}

TEST(GoogleWebRtcAvSync, CleanPathMarkersAndStatsAgree)
{
    const auto run = run_av_sync_call(driscord::sfu::RtpFaultConfig { },
        "av_sync_clean");
    expect_sync_within(run, "av_sync.clean", 80.0);

    (void)test_util::gate_ge("av_sync.clean.video_playout_ts_seen",
        run.video_playout_ts_seen ? 1.0 : 0.0, 1.0);
    (void)test_util::gate_ge("av_sync.clean.audio_playout_ts_seen",
        run.audio_playout_ts_seen ? 1.0 : 0.0, 1.0);
    if (run.stats_skew_ms.size() >= 3) {
        std::vector<double> magnitudes;
        magnitudes.reserve(run.stats_skew_ms.size());
        for (const double skew : run.stats_skew_ms) {
            magnitudes.push_back(std::abs(skew));
        }
        EXPECT_TRUE(test_util::gate_le("av_sync.clean.stats_skew_p95_ms",
            test_util::percentile(magnitudes, 95.0), 45.0));
        (void)test_util::gate_le("av_sync.clean.methods_disagreement_ms",
            std::abs(test_util::percentile(magnitudes, 50.0)
                - run.markers.abs_p50_ms),
            20.0);
    }
}

TEST(GoogleWebRtcAvSync, WifiBurstSyncHolds)
{
    const auto run = run_av_sync_call(test_util::wifi_burst_profile(59),
        "av_sync_wifi_burst", 0.30);
    expect_sync_within(run, "av_sync.wifi_burst", 100.0, kEventCount / 3);
}
