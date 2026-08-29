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
#include "webrtc/google_webrtc_voice_session.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using test_util::EventCollector;
using test_util::SignalingServerFixture;
using test_util::Waiter;

class DecodedToneProbe {
public:
    void observe(const driscord::media::DecodedAudioFrameView& frame)
    {
        const auto non_silent = std::count_if(frame.samples.begin(),
            frame.samples.end(), [](int16_t sample) {
                return std::abs(static_cast<int>(sample)) > 100;
            });
        if (non_silent == 0) {
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            ++frames_;
            sample_rate_hz_ = frame.sample_rate_hz;
            for (size_t i = 0; i < frame.samples.size(); i += frame.channels) {
                const int sample = frame.samples[i];
                int polarity = last_polarity_;
                if (sample > 500) {
                    polarity = 1;
                } else if (sample < -500) {
                    polarity = -1;
                }
                if (last_polarity_ < 0 && polarity > 0) {
                    ++positive_crossings_;
                }
                last_polarity_ = polarity;
                ++analyzed_samples_;
            }
        }
        changed_.notify_all();
    }

    bool wait_for_frames(size_t minimum, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout,
            [&] { return frames_ >= minimum; });
    }

    [[nodiscard]] size_t frames() const
    {
        std::scoped_lock lock(mutex_);
        return frames_;
    }

    [[nodiscard]] double estimated_frequency_hz() const
    {
        std::scoped_lock lock(mutex_);
        return analyzed_samples_ == 0
            ? 0.0
            : static_cast<double>(positive_crossings_) * sample_rate_hz_
                / static_cast<double>(analyzed_samples_);
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    size_t frames_ = 0;
    int sample_rate_hz_ = 0;
    size_t positive_crossings_ = 0;
    size_t analyzed_samples_ = 0;
    int last_polarity_ = 0;
};

std::vector<int16_t> make_tone_frame(double frequency_hz,
    size_t frame_index,
    int amplitude = 12'000,
    int sample_rate_hz = 48'000)
{
    constexpr double kPi = 3.14159265358979323846;
    const size_t samples_per_frame = static_cast<size_t>(sample_rate_hz / 100);
    std::vector<int16_t> result(samples_per_frame);
    const size_t offset = frame_index * samples_per_frame;
    for (size_t i = 0; i < samples_per_frame; ++i) {
        result[i] = static_cast<int16_t>(std::lround(amplitude
            * std::sin(2.0 * kPi * frequency_hz
                * static_cast<double>(offset + i) / sample_rate_hz)));
    }
    return result;
}

struct VoiceScenarioMetrics {
    size_t decoded_frames = 0;
    double estimated_frequency_hz = 0.0;
    double concealment_rate = 0.0;
    double jitter_buffer_target_delay_seconds = 0.0;
    double jitter_buffer_delay_per_emitted_seconds = 0.0;
    size_t errors = 0;
};

VoiceScenarioMetrics run_voice_scenario(driscord::sfu::RtpFaultConfig faults,
    size_t decoded_frame_floor)
{
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcVoiceSession;
    using driscord::media::VoiceSessionCallbacks;
    using driscord::media::VoiceSessionConfig;
    using driscord::media::VoiceSessionStats;

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
    EventCollector<std::pair<std::string, std::optional<std::string>>> bindings;
    EventCollector<std::string> errors;
    DecodedToneProbe decoded;
    VoiceScenarioMetrics metrics;

    auto make_voice = [&errors](GoogleWebRtcRuntime& runtime,
                          Transport& transport,
                          Waiter& connected,
                          bool microphone_enabled,
                          DecodedToneProbe* probe) {
        VoiceSessionCallbacks callbacks;
        callbacks.on_offer = [&transport](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Voice, sdp);
        };
        callbacks.on_candidate =
            [&transport](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Voice,
                    candidate, mid);
            };
        callbacks.on_state =
            [&connected](driscord::media::VoiceConnectionState state) {
                if (state == driscord::media::VoiceConnectionState::Connected) {
                    connected.signal();
                }
            };
        callbacks.on_error = [&errors](std::string message) {
            errors.push(std::move(message));
        };
        if (probe) {
            callbacks.on_remote_audio =
                [probe](std::string_view,
                    driscord::media::DecodedAudioFrameView frame) {
                    probe->observe(frame);
                };
        }
        return std::make_unique<GoogleWebRtcVoiceSession>(runtime,
            VoiceSessionConfig {
                .remote_track_slots = 2,
                .microphone_enabled = microphone_enabled,
            },
            std::move(callbacks));
    };

    auto publisher_voice = make_voice(publisher_runtime, publisher_transport,
        publisher_connected, true, nullptr);
    auto listener_voice = make_voice(listener_runtime, listener_transport,
        listener_connected, false, &decoded);

    auto connect_voice = [](Transport& transport,
                             GoogleWebRtcVoiceSession& voice) {
        transport.on_media_answer(
            [&voice](signaling::ConnectionId connection,
                const std::string& sdp) {
                if (connection == signaling::ConnectionId::Voice) {
                    voice.apply_answer(sdp);
                }
            });
        transport.on_media_candidate(
            [&voice](signaling::ConnectionId connection,
                const std::string& candidate,
                const std::string& mid) {
                if (connection == signaling::ConnectionId::Voice) {
                    voice.add_remote_candidate(candidate, mid);
                }
            });
    };
    connect_voice(publisher_transport, *publisher_voice);
    connect_voice(listener_transport, *listener_voice);
    listener_transport.on_media_track_binding(
        [&bindings](signaling::ConnectionId connection,
            const std::string& mid,
            const std::optional<driscord::PeerId>& peer_id) {
            if (connection == signaling::ConnectionId::Voice) {
                bindings.push({ mid,
                    peer_id ? std::optional<std::string>(peer_id->value)
                            : std::optional<std::string>() });
            }
        });

    EXPECT_TRUE(publisher_transport.connect(server.ws_url()));
    EXPECT_TRUE(listener_transport.connect(server.ws_url()));
    EXPECT_TRUE(test_util::wait_for_local_id(publisher_transport));
    EXPECT_TRUE(test_util::wait_for_local_id(listener_transport));
    EXPECT_TRUE(publisher_voice->start());
    EXPECT_TRUE(listener_voice->start());
    EXPECT_TRUE(publisher_connected.wait_for());
    EXPECT_TRUE(listener_connected.wait_for());
    EXPECT_TRUE(bindings.wait_for_count(1));
    std::string bound_mid;
    for (const auto& [mid, peer] : bindings.snapshot()) {
        if (peer) {
            bound_mid = mid;
        }
    }
    EXPECT_FALSE(bound_mid.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    uint64_t baseline_concealed = 0;
    uint64_t baseline_total = 0;
    const auto baseline = test_util::sample_stats<VoiceSessionStats>(
        *listener_voice, 1);
    EXPECT_EQ(baseline.size(), 1u);
    if (!baseline.empty()) {
        for (const auto& inbound : baseline.front().inbound) {
            if (inbound.mid == bound_mid) {
                baseline_concealed = inbound.concealed_samples;
                baseline_total = inbound.audio.total_samples_received;
            }
        }
    }

    constexpr size_t kToneFrames = 400;
    for (size_t frame = 0; frame < kToneFrames; ++frame) {
        EXPECT_TRUE(publisher_runtime.submit_recorded_audio_10ms(
            make_tone_frame(440.0, frame)));
    }
    EXPECT_TRUE(decoded.wait_for_frames(decoded_frame_floor,
        std::chrono::seconds(20)))
        << "decoded " << decoded.frames() << "/" << kToneFrames
        << " frames, floor " << decoded_frame_floor;

    const auto samples = test_util::sample_stats<VoiceSessionStats>(
        *listener_voice, 1);
    EXPECT_EQ(samples.size(), 1u);
    if (!samples.empty()) {
        for (const auto& inbound : samples.front().inbound) {
            if (inbound.mid != bound_mid) {
                continue;
            }
            const uint64_t delta_concealed
                = inbound.concealed_samples - baseline_concealed;
            const uint64_t delta_total
                = inbound.audio.total_samples_received - baseline_total;
            metrics.concealment_rate = delta_total == 0
                ? 0.0
                : static_cast<double>(delta_concealed)
                    / static_cast<double>(delta_total);
            metrics.jitter_buffer_target_delay_seconds
                = inbound.jitter_buffer_target_delay_seconds;
            metrics.jitter_buffer_delay_per_emitted_seconds
                = inbound.jitter_buffer_emitted_count == 0
                ? 0.0
                : inbound.jitter_buffer_delay_seconds
                    / static_cast<double>(inbound.jitter_buffer_emitted_count);
        }
    }
    metrics.decoded_frames = decoded.frames();
    metrics.estimated_frequency_hz = decoded.estimated_frequency_hz();
    metrics.errors = errors.snapshot().size();

    publisher_voice->close();
    listener_voice->close();
    publisher_transport.disconnect();
    listener_transport.disconnect();
    return metrics;
}

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

    bool wait_for_frames(size_t minimum, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout,
            [&] { return frames_ >= minimum; });
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    size_t frames_ = 0;
};

uint64_t total_keyframe_requests(SignalingServerFixture& server)
{
    const auto [status, body] = server.http_get("/media_stats");
    if (status != 200) {
        return 0;
    }
    const auto stats = nlohmann::json::parse(body, nullptr, false);
    if (stats.is_discarded()) {
        return 0;
    }
    uint64_t total = 0;
    for (const auto& [_, entry] : stats.items()) {
        if (entry.contains("screen")) {
            total += entry["screen"].value("keyframeRequests", 0);
        }
    }
    return total;
}

}

TEST(GoogleWebRtcNetworkScenarios, WifiBurstVoiceSurvives)
{
    const auto metrics = run_voice_scenario(
        test_util::wifi_burst_profile(31), 220);
    EXPECT_EQ(metrics.errors, 0u);
    EXPECT_NEAR(metrics.estimated_frequency_hz, 440.0, 140.0);
    EXPECT_TRUE(test_util::gate_le("voice.wifi_burst.concealment_rate",
        metrics.concealment_rate, 0.20));
}

TEST(GoogleWebRtcNetworkScenarios, JitterGrowsTheJitterBufferWithoutStalling)
{
    const auto clean = run_voice_scenario(test_util::clean_profile(), 250);
    const auto jittered = run_voice_scenario(
        test_util::lte_good_profile(47), 220);

    EXPECT_EQ(clean.errors, 0u);
    EXPECT_EQ(jittered.errors, 0u);
    EXPECT_NEAR(jittered.estimated_frequency_hz, 440.0, 140.0);
    EXPECT_GT(jittered.jitter_buffer_target_delay_seconds,
        clean.jitter_buffer_target_delay_seconds);
    EXPECT_TRUE(test_util::gate_le("voice.lte_good.concealment_rate",
        jittered.concealment_rate, 0.20));
}

TEST(GoogleWebRtcNetworkScenarios, ScreenBlackoutRecoversWithoutReconnect)
{
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcScreenSession;
    using driscord::media::ScreenConnectionState;
    using driscord::media::ScreenSessionCallbacks;
    using driscord::media::ScreenSessionConfig;

    SignalingServerFixture server { test_util::clean_profile() };

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
    EventCollector<ScreenConnectionState> listener_states;
    DecodedFrameCounter decoded;

    auto make_screen = [&](GoogleWebRtcRuntime& runtime,
                           Transport& transport,
                           Waiter& connected,
                           bool sharing,
                           DecodedFrameCounter* probe,
                           EventCollector<ScreenConnectionState>* states) {
        ScreenSessionCallbacks callbacks;
        callbacks.on_offer = [&transport](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Screen, sdp);
        };
        callbacks.on_candidate =
            [&transport](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Screen,
                    candidate, mid);
            };
        callbacks.on_state = [&connected, states](ScreenConnectionState state) {
            if (states) {
                states->push(state);
            }
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
        publisher_connected, true, nullptr, nullptr);
    auto listener_screen = make_screen(listener_runtime, listener_transport,
        listener_connected, false, &decoded, &listener_states);

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

    ASSERT_TRUE(publisher_transport.connect(server.ws_url()));
    ASSERT_TRUE(listener_transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(publisher_transport));
    ASSERT_TRUE(test_util::wait_for_local_id(listener_transport));
    ASSERT_TRUE(publisher_screen->start());
    ASSERT_TRUE(listener_screen->start());
    ASSERT_TRUE(publisher_connected.wait_for());
    ASSERT_TRUE(listener_connected.wait_for());
    publisher_transport.send_streaming_start();
    ASSERT_TRUE(streaming_publishers.wait_for_count(1));
    listener_transport.send_watch_start(publisher_transport.local_id());
    ASSERT_TRUE(bindings.wait_for_count(2));

    constexpr int kWidth = 320;
    constexpr int kHeight = 180;
    std::atomic<bool> feeding { true };
    std::thread feeder([&] {
        const auto epoch
            = std::chrono::steady_clock::now().time_since_epoch();
        const int64_t start_us
            = std::chrono::duration_cast<std::chrono::microseconds>(epoch)
                  .count();
        for (size_t frame = 0; feeding; ++frame) {
            const auto bgra = make_bgra_frame(kWidth, kHeight, 31, frame);
            (void)publisher_screen->submit_bgra_frame(bgra, kWidth, kHeight,
                kWidth * 4, start_us + static_cast<int64_t>(frame) * 33'333);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });

    ASSERT_TRUE(decoded.wait_for_frames(30, std::chrono::seconds(15)));
    const uint64_t keyframes_before = total_keyframe_requests(server);

    server.set_fault_config(test_util::link_down_profile());
    std::this_thread::sleep_for(std::chrono::seconds(4));
    const size_t frames_during_blackout_start = decoded.frames();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const size_t frames_during_blackout_end = decoded.frames();
    EXPECT_LE(frames_during_blackout_end - frames_during_blackout_start, 2u)
        << "blackout leaked media";

    server.set_fault_config(test_util::clean_profile());
    const size_t frames_at_restore = decoded.frames();
    EXPECT_TRUE(decoded.wait_for_frames(frames_at_restore + 30,
        std::chrono::seconds(10)))
        << "decode did not resume after the blackout lifted";

    feeding = false;
    feeder.join();

    EXPECT_GT(total_keyframe_requests(server), keyframes_before);
    for (const auto state : listener_states.snapshot()) {
        EXPECT_NE(state, ScreenConnectionState::Failed);
        EXPECT_NE(state, ScreenConnectionState::Closed);
    }
    EXPECT_TRUE(errors.snapshot().empty());

    publisher_screen->close();
    listener_screen->close();
    publisher_transport.disconnect();
    listener_transport.disconnect();
}
