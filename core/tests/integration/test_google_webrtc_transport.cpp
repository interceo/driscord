#include "headless_audio.hpp"
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
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using test_util::EventCollector;
using test_util::SignalingServerFixture;
using test_util::Waiter;

class GoogleWebRtcTransportTest : public ::testing::Test {
protected:
    SignalingServerFixture server { driscord::sfu::RtpFaultConfig {
        .drop_every_nth = 11,
        .reorder_every_nth = 7,
    } };
};

class MultiTrackAudioProbe {
public:
    void observe(std::string_view mid,
        const driscord::media::DecodedAudioFrameView& frame)
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
            auto& observation = observations_[std::string(mid)];
            ++observation.frames;
            observation.non_silent_samples += static_cast<size_t>(non_silent);
            observation.sample_rate_hz = frame.sample_rate_hz;
            observation.channels = frame.channels;
            for (size_t i = 0; i < frame.samples.size(); i += frame.channels) {
                const int sample = frame.samples[i];
                int polarity = observation.last_polarity;
                if (sample > 500) {
                    polarity = 1;
                } else if (sample < -500) {
                    polarity = -1;
                }
                if (observation.last_polarity < 0 && polarity > 0) {
                    ++observation.positive_crossings;
                }
                observation.last_polarity = polarity;
                ++observation.analyzed_samples;
            }
        }
        changed_.notify_all();
    }

    bool wait_for_active_mids(size_t count,
        std::chrono::milliseconds timeout = test_util::kDefaultTimeout)
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] {
            return std::count_if(observations_.begin(), observations_.end(),
                       [](const auto& item) { return item.second.frames >= 5; })
                >= static_cast<std::ptrdiff_t>(count);
        });
    }

    struct Observation {
        size_t frames = 0;
        size_t non_silent_samples = 0;
        int sample_rate_hz = 0;
        size_t channels = 0;
        size_t positive_crossings = 0;
        size_t analyzed_samples = 0;
        int last_polarity = 0;

        [[nodiscard]] double estimated_frequency_hz() const
        {
            return analyzed_samples == 0
                ? 0.0
                : static_cast<double>(positive_crossings) * sample_rate_hz
                    / static_cast<double>(analyzed_samples);
        }
    };

    std::map<std::string, Observation> snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return observations_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, Observation> observations_;
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

} // namespace

TEST_F(GoogleWebRtcTransportTest, VoiceConnectsToLibdatachannelSfu)
{
    driscord::media::GoogleWebRtcRuntime runtime(test_util::headless_audio_runtime());
    Transport transport;
    Waiter connected;
    std::mutex error_mutex;
    std::string callback_error;

    driscord::media::VoiceSessionCallbacks callbacks;
    callbacks.on_offer = [&transport](std::string sdp) {
        transport.send_media_offer(signaling::ConnectionId::Voice, sdp);
    };
    callbacks.on_candidate =
        [&transport](std::string candidate, std::string mid) {
            transport.send_media_candidate(signaling::ConnectionId::Voice,
                candidate, mid);
        };
    callbacks.on_state = [&connected](driscord::media::VoiceConnectionState state) {
        if (state == driscord::media::VoiceConnectionState::Connected) {
            connected.signal();
        }
    };
    callbacks.on_error = [&](std::string message) {
        std::scoped_lock lock(error_mutex);
        callback_error = std::move(message);
    };

    driscord::media::GoogleWebRtcVoiceSession voice(runtime,
        { .remote_track_slots = 2, .microphone_enabled = false },
        std::move(callbacks));
    transport.on_media_answer(
        [&voice](signaling::ConnectionId connection, const std::string& sdp) {
            if (connection == signaling::ConnectionId::Voice) {
                voice.apply_answer(sdp);
            }
        });
    transport.on_media_candidate([&voice](signaling::ConnectionId connection,
                                     const std::string& candidate,
                                     const std::string& mid) {
        if (connection == signaling::ConnectionId::Voice) {
            voice.add_remote_candidate(candidate, mid);
        }
    });

    ASSERT_TRUE(transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(transport));
    ASSERT_TRUE(voice.start());
    ASSERT_TRUE(connected.wait_for());
    {
        std::scoped_lock lock(error_mutex);
        EXPECT_TRUE(callback_error.empty()) << callback_error;
    }

    voice.close();
    transport.disconnect();
}

TEST_F(GoogleWebRtcTransportTest, VoiceSlotsBindThreeParticipantsIndependently)
{
    using Binding = std::pair<std::string, std::optional<std::string>>;
    driscord::media::GoogleWebRtcRuntime runtime(test_util::headless_audio_runtime());
    Transport first_transport;
    Transport second_transport;
    Transport third_transport;
    Waiter first_connected;
    Waiter second_connected;
    Waiter third_connected;
    EventCollector<Binding> first_bindings;
    EventCollector<Binding> second_bindings;
    EventCollector<Binding> third_bindings;

    auto make_voice = [&runtime](Transport& transport, Waiter& connected) {
        driscord::media::VoiceSessionCallbacks callbacks;
        callbacks.on_offer = [&transport](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Voice, sdp);
        };
        callbacks.on_candidate =
            [&transport](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Voice,
                    candidate, mid);
            };
        callbacks.on_state = [&connected](
                                 driscord::media::VoiceConnectionState state) {
            if (state == driscord::media::VoiceConnectionState::Connected) {
                connected.signal();
            }
        };
        return std::make_unique<driscord::media::GoogleWebRtcVoiceSession>(
            runtime,
            driscord::media::VoiceSessionConfig {
                .remote_track_slots = 2,
                .microphone_enabled = false,
            },
            std::move(callbacks));
    };

    auto first_voice = make_voice(first_transport, first_connected);
    auto second_voice = make_voice(second_transport, second_connected);
    auto third_voice = make_voice(third_transport, third_connected);
    auto connect_voice = [](Transport& transport,
                             std::unique_ptr<driscord::media::GoogleWebRtcVoiceSession>& voice,
                             EventCollector<Binding>& bindings) {
        auto* voice_slot = &voice;
        transport.on_media_answer(
            [voice_slot](signaling::ConnectionId connection,
                const std::string& sdp) {
                if (connection == signaling::ConnectionId::Voice
                    && *voice_slot) {
                    (*voice_slot)->apply_answer(sdp);
                }
            });
        transport.on_media_candidate(
            [voice_slot](signaling::ConnectionId connection,
                const std::string& candidate,
                const std::string& mid) {
                if (connection == signaling::ConnectionId::Voice
                    && *voice_slot) {
                    (*voice_slot)->add_remote_candidate(candidate, mid);
                }
            });
        transport.on_media_track_binding(
            [&bindings](signaling::ConnectionId connection,
                const std::string& mid,
                const std::optional<driscord::PeerId>& peer_id) {
                if (connection == signaling::ConnectionId::Voice) {
                    bindings.push({ mid,
                        peer_id ? std::optional<std::string>(peer_id->value)
                                : std::optional<std::string>() });
                }
            });
    };
    connect_voice(first_transport, first_voice, first_bindings);
    connect_voice(second_transport, second_voice, second_bindings);
    connect_voice(third_transport, third_voice, third_bindings);

    ASSERT_TRUE(first_transport.connect(server.ws_url()));
    ASSERT_TRUE(second_transport.connect(server.ws_url()));
    ASSERT_TRUE(third_transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(first_transport));
    ASSERT_TRUE(test_util::wait_for_local_id(second_transport));
    ASSERT_TRUE(test_util::wait_for_local_id(third_transport));
    const std::string first_id = first_transport.local_id();
    const std::string second_id = second_transport.local_id();
    const std::string third_id = third_transport.local_id();

    ASSERT_TRUE(first_voice->start());
    ASSERT_TRUE(second_voice->start());
    ASSERT_TRUE(third_voice->start());
    ASSERT_TRUE(first_connected.wait_for());
    ASSERT_TRUE(second_connected.wait_for());
    ASSERT_TRUE(third_connected.wait_for());
    ASSERT_TRUE(first_bindings.wait_for_count(2));
    ASSERT_TRUE(second_bindings.wait_for_count(2));
    ASSERT_TRUE(third_bindings.wait_for_count(2));

    auto assert_bindings = [](const EventCollector<Binding>& bindings,
                               std::set<std::string> expected_peers) {
        const auto snapshot = bindings.snapshot();
        std::set<std::string> actual_peers;
        std::set<std::string> mids;
        for (const auto& [mid, peer] : snapshot) {
            if (peer) {
                actual_peers.insert(*peer);
                mids.insert(mid);
                EXPECT_NE(mid, "0");
            }
        }
        EXPECT_EQ(actual_peers, expected_peers);
        EXPECT_EQ(mids.size(), expected_peers.size());
    };
    assert_bindings(first_bindings, { second_id, third_id });
    assert_bindings(second_bindings, { first_id, third_id });
    assert_bindings(third_bindings, { first_id, second_id });

    // Recreating the client PeerConnection while keeping the signaling
    // session replaces server-side tracks. The stable subscriber slot must be
    // rebound to the new RTP source rather than retaining the old timeline.
    third_voice->close();
    third_voice = make_voice(third_transport, third_connected);
    ASSERT_TRUE(third_voice->start());
    ASSERT_TRUE(first_bindings.wait_for_count(3));
    ASSERT_TRUE(second_bindings.wait_for_count(3));
    EXPECT_EQ(first_bindings.snapshot().back().second, third_id);
    EXPECT_EQ(second_bindings.snapshot().back().second, third_id);

    // Churn must clear the exact slot before it can be reused by a later peer.
    third_voice->close();
    third_transport.disconnect();
    ASSERT_TRUE(first_bindings.wait_for_count(4));
    ASSERT_TRUE(second_bindings.wait_for_count(4));
    EXPECT_FALSE(first_bindings.snapshot().back().second);
    EXPECT_FALSE(second_bindings.snapshot().back().second);

    Transport fourth_transport;
    Waiter fourth_connected;
    EventCollector<Binding> fourth_bindings;
    auto fourth_voice = make_voice(fourth_transport, fourth_connected);
    connect_voice(fourth_transport, fourth_voice, fourth_bindings);
    ASSERT_TRUE(fourth_transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(fourth_transport));
    const std::string fourth_id = fourth_transport.local_id();
    ASSERT_TRUE(fourth_voice->start());
    ASSERT_TRUE(fourth_connected.wait_for());
    ASSERT_TRUE(first_bindings.wait_for_count(5));
    ASSERT_TRUE(second_bindings.wait_for_count(5));
    EXPECT_EQ(first_bindings.snapshot().back().second, fourth_id);
    EXPECT_EQ(second_bindings.snapshot().back().second, fourth_id);

    first_voice->close();
    second_voice->close();
    fourth_voice->close();
    first_transport.disconnect();
    second_transport.disconnect();
    fourth_transport.disconnect();
}

TEST_F(GoogleWebRtcTransportTest, RoutesAndDecodesTwoConcurrentPcmSources)
{
    using Binding = std::pair<std::string, std::optional<std::string>>;
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcVoiceSession;
    using driscord::media::VoiceSessionCallbacks;
    using driscord::media::VoiceSessionConfig;
    using driscord::media::VoiceSessionStats;

    const driscord::media::GoogleWebRtcRuntimeConfig runtime_config {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
    GoogleWebRtcRuntime first_runtime(runtime_config);
    GoogleWebRtcRuntime second_runtime(runtime_config);
    GoogleWebRtcRuntime listener_runtime(runtime_config);
    Transport first_transport;
    Transport second_transport;
    Transport listener_transport;
    Waiter first_connected;
    Waiter second_connected;
    Waiter listener_connected;
    EventCollector<Binding> listener_bindings;
    EventCollector<std::string> listener_answers;
    EventCollector<std::string> errors;
    MultiTrackAudioProbe decoded_audio;

    auto make_voice = [&errors](GoogleWebRtcRuntime& runtime,
                          Transport& transport,
                          Waiter& connected,
                          bool microphone_enabled,
                          MultiTrackAudioProbe* probe = nullptr) {
        VoiceSessionCallbacks callbacks;
        callbacks.on_offer = [&transport](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Voice, sdp);
        };
        callbacks.on_candidate =
            [&transport](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Voice,
                    candidate, mid);
            };
        callbacks.on_state = [&connected](
                                 driscord::media::VoiceConnectionState state) {
            if (state == driscord::media::VoiceConnectionState::Connected) {
                connected.signal();
            }
        };
        callbacks.on_error = [&errors](std::string message) {
            errors.push(std::move(message));
        };
        if (probe) {
            callbacks.on_remote_audio = [probe](std::string_view mid,
                                            driscord::media::DecodedAudioFrameView frame) {
                probe->observe(mid, frame);
            };
        }
        return std::make_unique<GoogleWebRtcVoiceSession>(runtime,
            VoiceSessionConfig {
                .remote_track_slots = 2,
                .microphone_enabled = microphone_enabled,
            },
            std::move(callbacks));
    };

    auto first_voice = make_voice(first_runtime, first_transport,
        first_connected, true);
    auto second_voice = make_voice(second_runtime, second_transport,
        second_connected, true);
    auto listener_voice = make_voice(listener_runtime, listener_transport,
        listener_connected, false, &decoded_audio);

    auto connect_voice = [](Transport& transport,
                             GoogleWebRtcVoiceSession& voice,
                             EventCollector<std::string>* answers = nullptr) {
        transport.on_media_answer(
            [&voice, answers](
                signaling::ConnectionId connection, const std::string& sdp) {
                if (connection == signaling::ConnectionId::Voice) {
                    if (answers) {
                        answers->push(sdp);
                    }
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
    connect_voice(first_transport, *first_voice);
    connect_voice(second_transport, *second_voice);
    connect_voice(listener_transport, *listener_voice, &listener_answers);
    listener_transport.on_media_track_binding(
        [&listener_bindings](signaling::ConnectionId connection,
            const std::string& mid,
            const std::optional<driscord::PeerId>& peer_id) {
            if (connection == signaling::ConnectionId::Voice) {
                listener_bindings.push({ mid,
                    peer_id ? std::optional<std::string>(peer_id->value)
                            : std::optional<std::string>() });
            }
        });

    ASSERT_TRUE(first_transport.connect(server.ws_url()));
    ASSERT_TRUE(second_transport.connect(server.ws_url()));
    ASSERT_TRUE(listener_transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(first_transport));
    ASSERT_TRUE(test_util::wait_for_local_id(second_transport));
    ASSERT_TRUE(test_util::wait_for_local_id(listener_transport));
    const std::set<std::string> expected_publishers {
        first_transport.local_id(), second_transport.local_id()
    };
    const std::map<std::string, double> expected_frequency_by_peer {
        { first_transport.local_id(), 440.0 },
        { second_transport.local_id(), 880.0 },
    };

    ASSERT_TRUE(first_voice->start());
    ASSERT_TRUE(second_voice->start());
    ASSERT_TRUE(listener_voice->start());
    ASSERT_TRUE(first_connected.wait_for());
    ASSERT_TRUE(second_connected.wait_for());
    ASSERT_TRUE(listener_connected.wait_for());
    ASSERT_TRUE(listener_bindings.wait_for_count(2));
    ASSERT_TRUE(listener_answers.wait_for_count(1));
    const auto answer = listener_answers.snapshot().front();
    EXPECT_EQ(answer.find("transport-cc"), std::string::npos);
    EXPECT_EQ(answer.find("transport-wide-cc"), std::string::npos);
    EXPECT_EQ(answer.find("goog-remb"), std::string::npos);

    std::map<std::string, std::string> peer_by_mid;
    for (const auto& [mid, peer] : listener_bindings.snapshot()) {
        if (peer) {
            peer_by_mid[mid] = *peer;
        }
    }
    std::set<std::string> bound_publishers;
    for (const auto& [mid, peer] : peer_by_mid) {
        (void)mid;
        bound_publishers.insert(peer);
    }
    ASSERT_EQ(bound_publishers, expected_publishers);

    // The official clocked TestAudioDeviceModule consumes one queued frame per
    // 10 ms, so feeding ahead avoids scheduler-dependent sleeps in the test.
    for (size_t frame = 0; frame < 250; ++frame) {
        ASSERT_TRUE(first_runtime.submit_recorded_audio_10ms(
            make_tone_frame(440.0, frame)));
        ASSERT_TRUE(second_runtime.submit_recorded_audio_10ms(
            make_tone_frame(880.0, frame)));
    }

    ASSERT_TRUE(decoded_audio.wait_for_active_mids(2));
    const auto observations = decoded_audio.snapshot();
    for (const auto& [mid, peer] : peer_by_mid) {
        (void)peer;
        const auto found = observations.find(mid);
        ASSERT_NE(found, observations.end()) << "missing decoded mid " << mid;
        EXPECT_GE(found->second.frames, 5u);
        EXPECT_GT(found->second.non_silent_samples, 0u);
        EXPECT_EQ(found->second.sample_rate_hz, 48'000);
        EXPECT_EQ(found->second.channels, 1u);
        EXPECT_NEAR(found->second.estimated_frequency_hz(),
            expected_frequency_by_peer.at(peer), 140.0)
            << "decoded tone was routed to the wrong mid " << mid;
    }

    EventCollector<VoiceSessionStats> listener_stats;
    EventCollector<VoiceSessionStats> first_stats;
    EventCollector<VoiceSessionStats> second_stats;
    ASSERT_TRUE(listener_voice->get_stats(
        [&listener_stats](VoiceSessionStats stats) {
            listener_stats.push(std::move(stats));
        }));
    ASSERT_TRUE(first_voice->get_stats([&first_stats](VoiceSessionStats stats) {
        first_stats.push(std::move(stats));
    }));
    ASSERT_TRUE(second_voice->get_stats(
        [&second_stats](VoiceSessionStats stats) {
            second_stats.push(std::move(stats));
        }));
    ASSERT_TRUE(listener_stats.wait_for_count(1));
    ASSERT_TRUE(first_stats.wait_for_count(1));
    ASSERT_TRUE(second_stats.wait_for_count(1));
    const auto stats = listener_stats.snapshot().front();
    EXPECT_GT(first_stats.snapshot().front().packets_sent, 0u);
    EXPECT_GT(first_stats.snapshot().front().bytes_sent, 0u);
    EXPECT_GT(second_stats.snapshot().front().packets_sent, 0u);
    EXPECT_GT(second_stats.snapshot().front().bytes_sent, 0u);
    std::set<std::string> active_stats_mids;
    int64_t packets_lost = 0;
    for (const auto& inbound : stats.inbound) {
        packets_lost += std::max<int64_t>(0, inbound.packets_lost);
        if (inbound.packets_received > 0 && inbound.bytes_received > 0
            && inbound.jitter_buffer_emitted_count > 0) {
            active_stats_mids.insert(inbound.mid);
        }
    }
    EXPECT_GT(packets_lost, 0);
    for (const auto& [mid, peer] : peer_by_mid) {
        (void)peer;
        EXPECT_TRUE(active_stats_mids.contains(mid))
            << "RTCStats has no active inbound stream for mid " << mid;
    }
    EXPECT_TRUE(errors.snapshot().empty());

    first_voice->close();
    second_voice->close();
    listener_voice->close();
    first_transport.disconnect();
    second_transport.disconnect();
    listener_transport.disconnect();
}
