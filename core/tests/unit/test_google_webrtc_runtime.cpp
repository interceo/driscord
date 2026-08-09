#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"
#include "webrtc/google_webrtc_voice_session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace {

size_t count_occurrences(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

struct OfferResult {
    std::mutex mutex;
    std::condition_variable changed;
    std::string offer;
    std::string error;
};

} // namespace

TEST(GoogleWebRtcRuntime, OwnsFactoryAndThreadsBehindPimpl)
{
    static_assert(!std::is_copy_constructible_v<driscord::media::GoogleWebRtcRuntime>);
    static_assert(std::is_nothrow_move_constructible_v<driscord::media::GoogleWebRtcRuntime>);

    driscord::media::GoogleWebRtcRuntime runtime;
    EXPECT_TRUE(runtime.ready());
}

TEST(GoogleWebRtcRuntime, InjectedAudioValidatesFrameShapeAndBoundsQueue)
{
    driscord::media::GoogleWebRtcRuntime runtime(
        driscord::media::GoogleWebRtcRuntimeConfig {
            .injected_audio_device = driscord::media::InjectedAudioDeviceConfig {
                .sample_rate_hz = 48'000,
                .channels = 1,
                .max_buffered_frames = 1,
                .on_rendered_audio = { },
            },
        });
    std::vector<int16_t> short_frame(479, 1);
    std::vector<int16_t> frame(480, 1);

    EXPECT_FALSE(runtime.submit_recorded_audio_10ms(short_frame));
    EXPECT_TRUE(runtime.submit_recorded_audio_10ms(frame));
    EXPECT_FALSE(runtime.submit_recorded_audio_10ms(frame));
}

TEST(GoogleWebRtcRuntime, PlatformAudioRejectsInjectedFrames)
{
    driscord::media::GoogleWebRtcRuntime runtime;
    const std::vector<int16_t> frame(480, 1);
    EXPECT_FALSE(runtime.submit_recorded_audio_10ms(frame));
}

TEST(GoogleWebRtcRuntime, RejectsUnsupportedInjectedAudioFormat)
{
    EXPECT_THROW(
        driscord::media::GoogleWebRtcRuntime(
            driscord::media::GoogleWebRtcRuntimeConfig {
                .injected_audio_device = driscord::media::InjectedAudioDeviceConfig {
                    .sample_rate_hz = 12'345,
                    .channels = 1,
                    .max_buffered_frames = 500,
                    .on_rendered_audio = { },
                },
            }),
        std::invalid_argument);
}

TEST(GoogleWebRtcVoiceSession, CreatesOneMicrophoneAndMultipleReceiveSlots)
{
    using namespace std::chrono_literals;
    using driscord::media::GoogleWebRtcVoiceSession;

    driscord::media::GoogleWebRtcRuntime runtime;
    auto result = std::make_shared<OfferResult>();
    driscord::media::VoiceSessionCallbacks callbacks;
    callbacks.on_offer = [result](std::string sdp) {
        {
            std::scoped_lock lock(result->mutex);
            result->offer = std::move(sdp);
        }
        result->changed.notify_all();
    };
    callbacks.on_error = [result](std::string message) {
        {
            std::scoped_lock lock(result->mutex);
            result->error = std::move(message);
        }
        result->changed.notify_all();
    };
    GoogleWebRtcVoiceSession session(runtime,
        { .remote_track_slots = 3, .microphone_enabled = false },
        std::move(callbacks));

    ASSERT_TRUE(session.start());
    {
        std::unique_lock lock(result->mutex);
        ASSERT_TRUE(result->changed.wait_for(lock, 10s,
            [&] { return !result->offer.empty() || !result->error.empty(); }));
        ASSERT_TRUE(result->error.empty()) << result->error;
        EXPECT_EQ(count_occurrences(result->offer, "m=audio "), 4u);
        EXPECT_EQ(count_occurrences(result->offer, "a=sendonly"), 1u);
        EXPECT_EQ(count_occurrences(result->offer, "a=recvonly"), 3u);
    }

    EXPECT_EQ(session.remote_track_slots(), 3u);
    EXPECT_FALSE(session.microphone_enabled());
    session.set_microphone_enabled(true);
    EXPECT_TRUE(session.microphone_enabled());
    session.close();
}

TEST(GoogleWebRtcVoiceSession, RejectsUnboundedSlotCount)
{
    driscord::media::GoogleWebRtcRuntime runtime;
    std::string error;
    driscord::media::VoiceSessionCallbacks callbacks;
    callbacks.on_error =
        [&](std::string message) { error = std::move(message); };
    driscord::media::GoogleWebRtcVoiceSession session(runtime,
        { .remote_track_slots = driscord::media::GoogleWebRtcVoiceSession::kMaxRemoteTrackSlots
                + 1 },
        std::move(callbacks));

    EXPECT_FALSE(session.start());
    EXPECT_NE(error.find("safety limit"), std::string::npos);
}

TEST(GoogleWebRtcVoiceSession, RejectsInvalidMicrophoneBitrate)
{
    driscord::media::GoogleWebRtcRuntime runtime;
    std::string error;
    driscord::media::VoiceSessionCallbacks callbacks;
    callbacks.on_error =
        [&](std::string message) { error = std::move(message); };
    driscord::media::GoogleWebRtcVoiceSession session(runtime,
        {
            .remote_track_slots = 1,
            .max_microphone_bitrate_bps = 0,
        },
        std::move(callbacks));

    EXPECT_FALSE(session.start());
    EXPECT_NE(error.find("bitrate"), std::string::npos);
}

TEST(GoogleWebRtcScreenSession, CreatesPairedSendAndReceiveTracks)
{
    using namespace std::chrono_literals;
    using driscord::media::GoogleWebRtcScreenSession;

    driscord::media::GoogleWebRtcRuntime runtime;
    auto result = std::make_shared<OfferResult>();
    driscord::media::ScreenSessionCallbacks callbacks;
    callbacks.on_offer = [result](std::string sdp) {
        {
            std::scoped_lock lock(result->mutex);
            result->offer = std::move(sdp);
        }
        result->changed.notify_all();
    };
    callbacks.on_error = [result](std::string message) {
        {
            std::scoped_lock lock(result->mutex);
            result->error = std::move(message);
        }
        result->changed.notify_all();
    };
    GoogleWebRtcScreenSession session(runtime,
        { .remote_stream_slots = 2, .sharing_enabled = false },
        std::move(callbacks));

    ASSERT_TRUE(session.start());
    {
        std::unique_lock lock(result->mutex);
        ASSERT_TRUE(result->changed.wait_for(lock, 10s,
            [&] { return !result->offer.empty() || !result->error.empty(); }));
        ASSERT_TRUE(result->error.empty()) << result->error;
        EXPECT_EQ(count_occurrences(result->offer, "m=video "), 3u);
        EXPECT_EQ(count_occurrences(result->offer, "m=audio "), 3u);
        EXPECT_EQ(count_occurrences(result->offer, "a=sendonly"), 2u);
        EXPECT_EQ(count_occurrences(result->offer, "a=recvonly"), 4u);
    }

    EXPECT_EQ(session.remote_stream_slots(), 2u);
    EXPECT_FALSE(session.sharing_enabled());
    session.set_sharing_enabled(true);
    EXPECT_TRUE(session.sharing_enabled());
    session.close();
}

TEST(GoogleWebRtcScreenSession, RejectsSystemAudioOnPlatformMicrophoneRuntime)
{
    driscord::media::GoogleWebRtcRuntime runtime;
    std::string error;
    driscord::media::ScreenSessionCallbacks callbacks;
    callbacks.on_error =
        [&](std::string message) { error = std::move(message); };
    driscord::media::GoogleWebRtcScreenSession session(runtime,
        { .remote_stream_slots = 1, .system_audio_enabled = true },
        std::move(callbacks));

    EXPECT_FALSE(session.start());
    EXPECT_NE(error.find("injected-audio"), std::string::npos);
}
