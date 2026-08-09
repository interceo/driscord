#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_client.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using test_util::EventCollector;
using test_util::SignalingServerFixture;
using test_util::Waiter;

class GoogleWebRtcScreenTransportTest : public ::testing::Test {
protected:
    SignalingServerFixture server { driscord::sfu::RtpFaultConfig {
        .drop_every_nth = 37,
        .reorder_every_nth = 11,
    } };
};

class MultiTrackVideoProbe {
public:
    void observe(std::string_view mid,
        const driscord::media::DecodedVideoFrameView& frame)
    {
        if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) {
            return;
        }
        size_t colored = 0;
        uint64_t red_sum = 0;
        uint64_t blue_sum = 0;
        for (size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
            red_sum += frame.rgba[i];
            blue_sum += frame.rgba[i + 2];
            if (frame.rgba[i] > 20 || frame.rgba[i + 1] > 20
                || frame.rgba[i + 2] > 20) {
                ++colored;
            }
        }
        if (colored == 0) {
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            auto& observation = observations_[std::string(mid)];
            ++observation.frames;
            observation.colored_pixels += colored;
            observation.red_sum += red_sum;
            observation.blue_sum += blue_sum;
            observation.sampled_pixels += frame.rgba.size() / 4;
            observation.width = frame.width;
            observation.height = frame.height;
        }
        changed_.notify_all();
    }

    bool has_active_mids(size_t count, size_t minimum_frames = 3) const
    {
        std::scoped_lock lock(mutex_);
        return std::count_if(observations_.begin(), observations_.end(),
                   [minimum_frames](const auto& item) {
                       return item.second.frames >= minimum_frames;
                   })
            >= static_cast<std::ptrdiff_t>(count);
    }

    bool wait_for_active_mids(size_t count,
        std::chrono::milliseconds timeout = test_util::kDefaultTimeout)
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] {
            return std::count_if(observations_.begin(), observations_.end(),
                       [](const auto& item) {
                           return item.second.frames >= 3;
                       })
                >= static_cast<std::ptrdiff_t>(count);
        });
    }

    bool wait_for_mid_frames(std::string_view mid,
        size_t minimum_frames,
        std::chrono::milliseconds timeout = test_util::kDefaultTimeout)
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] {
            const auto found = observations_.find(std::string(mid));
            return found != observations_.end()
                && found->second.frames >= minimum_frames;
        });
    }

    struct Observation {
        size_t frames = 0;
        size_t colored_pixels = 0;
        int width = 0;
        int height = 0;
        uint64_t red_sum = 0;
        uint64_t blue_sum = 0;
        size_t sampled_pixels = 0;

        [[nodiscard]] double red_minus_blue() const
        {
            return sampled_pixels == 0
                ? 0.0
                : (static_cast<double>(red_sum)
                      - static_cast<double>(blue_sum))
                    / static_cast<double>(sampled_pixels);
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

std::vector<uint8_t> make_bgra_frame(
    int width, int height, uint8_t seed, size_t frame_index)
{
    std::vector<uint8_t> result(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width)
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

} // namespace

TEST_F(GoogleWebRtcScreenTransportTest,
    RoutesAndDecodesTwoPairedScreenStreams)
{
    using Binding = std::pair<std::string, std::optional<std::string>>;
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcScreenSession;
    using driscord::media::ScreenConnectionState;
    using driscord::media::ScreenSessionCallbacks;
    using driscord::media::ScreenSessionConfig;
    using driscord::media::ScreenSessionStats;

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
    EventCollector<std::string> first_answers;
    EventCollector<std::string> listener_answers;
    EventCollector<std::string> errors;
    MultiTrackVideoProbe decoded_video;

    auto make_screen = [&errors](GoogleWebRtcRuntime& runtime,
                           Transport& transport,
                           Waiter& connected,
                           bool sharing,
                           MultiTrackVideoProbe* probe = nullptr) {
        ScreenSessionCallbacks callbacks;
        callbacks.on_offer = [&transport](std::string sdp) {
            transport.send_media_offer(
                signaling::ConnectionId::Screen, sdp);
        };
        callbacks.on_candidate =
            [&transport](std::string candidate, std::string mid) {
                transport.send_media_candidate(
                    signaling::ConnectionId::Screen, candidate, mid);
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
                [probe](std::string_view mid,
                    driscord::media::DecodedVideoFrameView frame) {
                    probe->observe(mid, frame);
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

    auto first_screen = make_screen(
        first_runtime, first_transport, first_connected, true);
    auto second_screen = make_screen(
        second_runtime, second_transport, second_connected, true);
    auto listener_screen = make_screen(listener_runtime, listener_transport,
        listener_connected, false, &decoded_video);

    auto connect_screen = [](Transport& transport,
                              std::unique_ptr<GoogleWebRtcScreenSession>& screen,
                              EventCollector<std::string>* answers = nullptr) {
        auto* screen_slot = &screen;
        transport.on_media_answer(
            [screen_slot, answers](signaling::ConnectionId connection,
                const std::string& sdp) {
                if (connection == signaling::ConnectionId::Screen
                    && *screen_slot) {
                    if (answers) {
                        answers->push(sdp);
                    }
                    (*screen_slot)->apply_answer(sdp);
                }
            });
        transport.on_media_candidate(
            [screen_slot](signaling::ConnectionId connection,
                const std::string& candidate,
                const std::string& mid) {
                if (connection == signaling::ConnectionId::Screen
                    && *screen_slot) {
                    (*screen_slot)->add_remote_candidate(candidate, mid);
                }
            });
    };
    connect_screen(first_transport, first_screen, &first_answers);
    connect_screen(second_transport, second_screen);
    connect_screen(listener_transport, listener_screen, &listener_answers);
    listener_transport.on_media_track_binding(
        [&listener_bindings](signaling::ConnectionId connection,
            const std::string& mid,
            const std::optional<driscord::PeerId>& peer_id) {
            if (connection == signaling::ConnectionId::Screen) {
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

    ASSERT_TRUE(first_screen->start());
    ASSERT_TRUE(second_screen->start());
    ASSERT_TRUE(listener_screen->start());
    ASSERT_TRUE(first_answers.wait_for_count(1));
    ASSERT_TRUE(first_connected.wait_for())
        << "callbacks="
        << (errors.snapshot().empty() ? std::string("none")
                                      : errors.snapshot().front())
        << "\nanswer:\n"
        << first_answers.snapshot().front();
    ASSERT_TRUE(second_connected.wait_for());
    ASSERT_TRUE(listener_connected.wait_for());
    ASSERT_TRUE(listener_answers.wait_for_count(1));
    const auto answer = listener_answers.snapshot().front();
    EXPECT_EQ(answer.find("transport-cc"), std::string::npos);
    EXPECT_EQ(answer.find("transport-wide-cc"), std::string::npos);
    EXPECT_EQ(answer.find("goog-remb"), std::string::npos);
    EXPECT_NE(answer.find("nack pli"), std::string::npos);

    first_transport.send_streaming_start();
    second_transport.send_streaming_start();
    listener_transport.send_watch_start(first_transport.local_id());
    ASSERT_TRUE(listener_bindings.wait_for_count(2));
    for (const auto& [_, publisher] : listener_bindings.snapshot()) {
        ASSERT_TRUE(publisher);
        EXPECT_EQ(*publisher, first_transport.local_id());
    }

    listener_transport.send_watch_start(second_transport.local_id());
    ASSERT_TRUE(listener_bindings.wait_for_count(4));

    std::map<std::string, std::set<std::string>> mids_by_publisher;
    for (const auto& [mid, publisher] : listener_bindings.snapshot()) {
        if (publisher) {
            mids_by_publisher[*publisher].insert(mid);
        }
    }
    ASSERT_EQ(mids_by_publisher.size(), 2u);
    for (const auto& publisher : expected_publishers) {
        ASSERT_TRUE(mids_by_publisher.contains(publisher));
        EXPECT_EQ(mids_by_publisher[publisher].size(), 2u)
            << "screen slot must bind one video and one audio mid";
    }

    constexpr int kWidth = 320;
    constexpr int kHeight = 180;
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t start_us = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
    size_t first_accepted = 0;
    size_t second_accepted = 0;
    for (size_t frame = 0;
        frame < 120
        && (frame < 30 || !decoded_video.has_active_mids(2));
        ++frame) {
        const auto first = make_bgra_frame(kWidth, kHeight, 31, frame);
        const auto second = make_bgra_frame(kWidth, kHeight, 113, frame);
        const int64_t timestamp_us = start_us + static_cast<int64_t>(frame) * 33'333;
        first_accepted += first_screen->submit_bgra_frame(first, kWidth,
            kHeight, kWidth * 4, timestamp_us);
        second_accepted += second_screen->submit_bgra_frame(second, kWidth,
            kHeight, kWidth * 4, timestamp_us);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    EXPECT_GT(first_accepted, 0u);
    EXPECT_GT(second_accepted, 0u);
    ASSERT_TRUE(decoded_video.wait_for_active_mids(2));
    const auto observations = decoded_video.snapshot();
    std::set<std::string> video_publishers;
    for (const auto& [mid, observation] : observations) {
        if (observation.frames < 3) {
            continue;
        }
        EXPECT_GT(observation.colored_pixels, 0u);
        EXPECT_EQ(observation.width, kWidth);
        EXPECT_EQ(observation.height, kHeight);
        for (const auto& [publisher, mids] : mids_by_publisher) {
            if (mids.contains(mid)) {
                video_publishers.insert(publisher);
                if (publisher == first_transport.local_id()) {
                    EXPECT_GT(observation.red_minus_blue(), 90.0)
                        << "first publisher decoded on wrong mid " << mid;
                } else if (publisher == second_transport.local_id()) {
                    EXPECT_LT(observation.red_minus_blue(), 65.0)
                        << "second publisher decoded on wrong mid " << mid;
                }
            }
        }
    }
    EXPECT_EQ(video_publishers, expected_publishers);

    // Replacing a publisher's Google PeerConnection on the same signaling
    // session must preserve the listener's stable output slot while rebasing
    // the new upstream RTP timeline and refreshing the PLI target.
    std::string first_video_mid;
    for (const auto& [mid, observation] : observations) {
        if (observation.frames >= 3
            && mids_by_publisher[first_transport.local_id()].contains(mid)) {
            first_video_mid = mid;
            break;
        }
    }
    ASSERT_FALSE(first_video_mid.empty());
    const size_t frames_before_restart
        = observations.at(first_video_mid).frames;
    first_screen->close();
    Waiter first_reconnected;
    first_screen = make_screen(
        first_runtime, first_transport, first_reconnected, true);
    ASSERT_TRUE(first_screen->start());
    ASSERT_TRUE(first_answers.wait_for_count(2));
    ASSERT_TRUE(first_reconnected.wait_for());
    ASSERT_TRUE(listener_bindings.wait_for_count(6));

    const auto restart_epoch = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t restart_us
        = std::chrono::duration_cast<std::chrono::microseconds>(restart_epoch)
              .count();
    size_t restarted_accepted = 0;
    for (size_t frame = 0;
        frame < 120
        && decoded_video.snapshot()[first_video_mid].frames
            < frames_before_restart + 3;
        ++frame) {
        const auto first = make_bgra_frame(kWidth, kHeight, 67, frame);
        restarted_accepted += first_screen->submit_bgra_frame(first, kWidth,
            kHeight, kWidth * 4,
            restart_us + static_cast<int64_t>(frame) * 33'333);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    EXPECT_GT(restarted_accepted, 0u);
    ASSERT_TRUE(decoded_video.wait_for_mid_frames(
        first_video_mid, frames_before_restart + 3));

    EventCollector<ScreenSessionStats> listener_stats;
    EventCollector<ScreenSessionStats> first_stats;
    EventCollector<ScreenSessionStats> second_stats;
    ASSERT_TRUE(listener_screen->get_stats(
        [&listener_stats](ScreenSessionStats stats) {
            listener_stats.push(std::move(stats));
        }));
    ASSERT_TRUE(first_screen->get_stats([&first_stats](ScreenSessionStats stats) {
        first_stats.push(std::move(stats));
    }));
    ASSERT_TRUE(second_screen->get_stats(
        [&second_stats](ScreenSessionStats stats) {
            second_stats.push(std::move(stats));
        }));
    ASSERT_TRUE(listener_stats.wait_for_count(1));
    ASSERT_TRUE(first_stats.wait_for_count(1));
    ASSERT_TRUE(second_stats.wait_for_count(1));
    EXPECT_GT(first_stats.snapshot().front().video_packets_sent, 0u);
    EXPECT_GT(first_stats.snapshot().front().video_bytes_sent, 0u);
    EXPECT_GT(second_stats.snapshot().front().video_packets_sent, 0u);
    EXPECT_GT(second_stats.snapshot().front().video_bytes_sent, 0u);

    const auto listener_stats_snapshot = listener_stats.snapshot();
    std::set<std::string> active_stats_mids;
    int64_t packets_lost = 0;
    for (const auto& inbound : listener_stats_snapshot.front().inbound) {
        packets_lost += std::max<int64_t>(0, inbound.packets_lost);
        if (inbound.video && inbound.packets_received > 0
            && inbound.bytes_received > 0 && inbound.frames_decoded > 0) {
            active_stats_mids.insert(inbound.mid);
        }
    }
    EXPECT_GT(packets_lost, 0);
    for (const auto& [mid, observation] : observations) {
        if (observation.frames >= 3) {
            EXPECT_TRUE(active_stats_mids.contains(mid))
                << "RTCStats has no active inbound video stream for mid "
                << mid;
        }
    }

    const size_t bindings_before_unwatch
        = listener_bindings.snapshot().size();
    listener_transport.send_watch_stop(first_transport.local_id());
    ASSERT_TRUE(
        listener_bindings.wait_for_count(bindings_before_unwatch + 2));
    std::map<std::string, std::optional<std::string>> current_bindings;
    for (const auto& [mid, publisher] : listener_bindings.snapshot()) {
        current_bindings[mid] = publisher;
    }
    size_t second_publisher_tracks = 0;
    size_t cleared_tracks = 0;
    for (const auto& [_, publisher] : current_bindings) {
        if (!publisher) {
            ++cleared_tracks;
        } else if (*publisher == second_transport.local_id()) {
            ++second_publisher_tracks;
        }
    }
    EXPECT_EQ(cleared_tracks, 2u);
    EXPECT_EQ(second_publisher_tracks, 2u);
    EXPECT_TRUE(errors.snapshot().empty());

    first_screen->close();
    second_screen->close();
    listener_screen->close();
    first_transport.disconnect();
    second_transport.disconnect();
    listener_transport.disconnect();
}

TEST_F(GoogleWebRtcScreenTransportTest,
    ReplaysTargetedSubscriptionAfterSignalingReconnect)
{
    using driscord::media::GoogleWebRtcRuntime;
    using driscord::media::GoogleWebRtcScreenSession;
    using driscord::media::ScreenConnectionState;
    using driscord::media::ScreenSessionCallbacks;
    using driscord::media::ScreenSessionConfig;

    const driscord::media::GoogleWebRtcRuntimeConfig runtime_config {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
    GoogleWebRtcRuntime publisher_runtime(runtime_config);
    Transport publisher_transport;
    Transport viewer_transport;
    Waiter publisher_connected;
    EventCollector<std::string> decoded_publishers;
    EventCollector<std::string> errors;

    ScreenSessionCallbacks publisher_callbacks;
    publisher_callbacks.on_offer = [&publisher_transport](std::string sdp) {
        publisher_transport.send_media_offer(
            signaling::ConnectionId::Screen, sdp);
    };
    publisher_callbacks.on_candidate =
        [&publisher_transport](std::string candidate, std::string mid) {
            publisher_transport.send_media_candidate(
                signaling::ConnectionId::Screen, candidate, mid);
        };
    publisher_callbacks.on_state = [&publisher_connected](
                                       ScreenConnectionState state) {
        if (state == ScreenConnectionState::Connected) {
            publisher_connected.signal();
        }
    };
    publisher_callbacks.on_error = [&errors](std::string message) {
        errors.push(std::move(message));
    };
    GoogleWebRtcScreenSession publisher(publisher_runtime,
        ScreenSessionConfig {
            .remote_stream_slots = 1,
            .sharing_enabled = true,
            .system_audio_enabled = false,
            .max_video_bitrate_bps = 1'500'000,
        },
        std::move(publisher_callbacks));
    publisher_transport.on_media_answer(
        [&publisher](signaling::ConnectionId connection,
            const std::string& sdp) {
            if (connection == signaling::ConnectionId::Screen) {
                publisher.apply_answer(sdp);
            }
        });
    publisher_transport.on_media_candidate(
        [&publisher](signaling::ConnectionId connection,
            const std::string& candidate,
            const std::string& mid) {
            if (connection == signaling::ConnectionId::Screen) {
                publisher.add_remote_candidate(candidate, mid);
            }
        });

    GoogleWebRtcClient viewer(viewer_transport,
        GoogleWebRtcClient::Callbacks {
            .on_frame = [&decoded_publishers](const std::string& peer,
                            const uint8_t*, int width, int height) {
                if (width > 0 && height > 0) {
                    decoded_publishers.push(peer);
                }
            },
            .on_frame_removed = { },
        });
    viewer.init_screen();

    ASSERT_TRUE(publisher_transport.connect(server.ws_url()));
    ASSERT_TRUE(viewer_transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(publisher_transport));
    ASSERT_TRUE(test_util::wait_for_local_id(viewer_transport));
    const std::string publisher_id = publisher_transport.local_id();
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(publisher_connected.wait_for());
    publisher_transport.send_streaming_start();
    viewer.join_stream(publisher_id);

    constexpr int kWidth = 320;
    constexpr int kHeight = 180;
    auto feed_until = [&](size_t expected_frames, size_t frame_offset) {
        const auto epoch = std::chrono::steady_clock::now().time_since_epoch();
        const int64_t start_us
            = std::chrono::duration_cast<std::chrono::microseconds>(epoch)
                  .count();
        for (size_t frame = 0;
            frame < 120
            && decoded_publishers.snapshot().size() < expected_frames;
            ++frame) {
            const auto image = make_bgra_frame(
                kWidth, kHeight, 79, frame + frame_offset);
            (void)publisher.submit_bgra_frame(image, kWidth, kHeight,
                kWidth * 4,
                start_us + static_cast<int64_t>(frame) * 33'333);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        return decoded_publishers.wait_for_count(
            expected_frames, std::chrono::seconds(2));
    };

    ASSERT_TRUE(feed_until(3, 0));
    for (const auto& peer : decoded_publishers.snapshot()) {
        EXPECT_EQ(peer, publisher_id);
    }

    viewer_transport.disconnect();
    const auto disconnect_deadline
        = std::chrono::steady_clock::now() + test_util::kDefaultTimeout;
    while (server.active_sessions() != 1
        && std::chrono::steady_clock::now() < disconnect_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_EQ(server.active_sessions(), 1u);
    ASSERT_TRUE(viewer_transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(viewer_transport));

    ASSERT_TRUE(feed_until(6, 200));
    for (const auto& peer : decoded_publishers.snapshot()) {
        EXPECT_EQ(peer, publisher_id);
    }
    EXPECT_TRUE(errors.snapshot().empty());

    viewer.deinit_screen();
    publisher.close();
    publisher_transport.disconnect();
    viewer_transport.disconnect();
}
