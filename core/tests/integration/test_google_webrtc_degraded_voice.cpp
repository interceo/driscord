#include "headless_audio.hpp"
#include "media_metrics.hpp"
#include "net_scenarios.hpp"
#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_voice_session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Reference-free degradation ladder for voice: the same one-publisher,
// one-listener loopback call at increasing Gilbert–Elliott loss. Gates lean
// on monotonicity across the rungs — far less runner-noise-sensitive than
// absolute thresholds — with one absolute tripwire on the clean rung.

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

    bool wait_for_frames(size_t minimum,
        std::chrono::milliseconds timeout = test_util::kDefaultTimeout)
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

struct VoiceRungMetrics {
    size_t decoded_frames = 0;
    double estimated_frequency_hz = 0.0;
    double concealment_rate = 0.0;
    uint64_t concealment_events = 0;
    uint64_t total_samples_received = 0;
    int64_t packets_lost = 0;
    double estimated_playout_timestamp_ms = -1.0;
    double jitter_buffer_target_delay_seconds = 0.0;
    size_t errors = 0;
};

VoiceRungMetrics run_voice_rung(driscord::sfu::RtpFaultConfig faults,
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
    VoiceRungMetrics metrics;

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

    // Baseline after the stream settles: NetEq's ramp-in right after connect
    // conceals a burst of samples on a perfectly clean link, so the rung
    // measures concealment as a delta over the steady tone window instead of
    // the cumulative counters.
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    uint64_t baseline_concealed = 0;
    uint64_t baseline_total = 0;
    uint64_t baseline_events = 0;
    const auto baseline = test_util::sample_stats<VoiceSessionStats>(
        *listener_voice, 1);
    EXPECT_EQ(baseline.size(), 1u);
    if (!baseline.empty()) {
        for (const auto& inbound : baseline.front().inbound) {
            if (inbound.mid == bound_mid) {
                baseline_concealed = inbound.concealed_samples;
                baseline_total = inbound.audio.total_samples_received;
                baseline_events = inbound.audio.concealment_events;
            }
        }
    }

    // 4 s of tone, fed ahead: the clocked TestAudioDeviceModule consumes one
    // queued 10 ms frame per tick.
    constexpr size_t kToneFrames = 400;
    for (size_t frame = 0; frame < kToneFrames; ++frame) {
        EXPECT_TRUE(publisher_runtime.submit_recorded_audio_10ms(
            make_tone_frame(440.0, frame)));
    }
    EXPECT_TRUE(decoded.wait_for_frames(decoded_frame_floor,
        std::chrono::seconds(15)))
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
            metrics.concealment_events
                = inbound.audio.concealment_events - baseline_events;
            metrics.total_samples_received
                = inbound.audio.total_samples_received;
            metrics.packets_lost = inbound.packets_lost;
            metrics.estimated_playout_timestamp_ms
                = inbound.rtp.estimated_playout_timestamp_ms;
            metrics.jitter_buffer_target_delay_seconds
                = inbound.jitter_buffer_target_delay_seconds;
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

} // namespace

TEST(GoogleWebRtcDegradedVoice, LossLadderDegradesMonotonically)
{
    // Deterministic Gilbert–Elliott rungs: mean burst of 4 packets at 0%, 2%
    // and 8% mean loss, fixed seed. Each rung is a fresh in-process SFU.
    const auto clean = run_voice_rung(driscord::sfu::RtpFaultConfig { }, 250);
    const auto light = run_voice_rung(
        driscord::sfu::RtpFaultConfig {
            .burst = test_util::burst_loss(0.02, 4.0, 17),
        },
        250);
    const auto heavy = run_voice_rung(
        driscord::sfu::RtpFaultConfig {
            .burst = test_util::burst_loss(0.08, 4.0, 17),
        },
        200);

    EXPECT_EQ(clean.errors, 0u);
    EXPECT_EQ(light.errors, 0u);
    EXPECT_EQ(heavy.errors, 0u);

    // The tone must remain identified through light loss; heavy loss only has
    // to keep audio flowing (goodput floor asserted inside the rung).
    EXPECT_NEAR(clean.estimated_frequency_hz, 440.0, 140.0);
    EXPECT_NEAR(light.estimated_frequency_hz, 440.0, 140.0);

    // New stats fields carry real data on the clean path. No assert on
    // estimated_playout_timestamp for voice: audio inbound needs the RTCP SR
    // NTP mapping to produce it, and whether the SFU preserves that mapping
    // is exactly the Phase-3 A/V-sync investigation (screen video already
    // reports it — see the screen ladder).
    EXPECT_GT(clean.total_samples_received, 0u);
    EXPECT_GT(clean.jitter_buffer_target_delay_seconds, 0.0);

    // Monotonicity is the primary gate: more injected loss must never conceal
    // fewer samples. The absolute tripwire guards only the clean rung.
    EXPECT_LE(clean.concealment_rate, light.concealment_rate);
    EXPECT_LE(light.concealment_rate, heavy.concealment_rate);
    EXPECT_GT(heavy.concealment_rate, clean.concealment_rate);
    EXPECT_TRUE(test_util::gate_le("voice.clean.concealment_rate",
        clean.concealment_rate, 0.01));

    // Heavy loss must be visible in the loss accounting, not silently eaten.
    EXPECT_GT(heavy.concealment_events, 0u);
    EXPECT_GT(heavy.packets_lost, 0);
}
