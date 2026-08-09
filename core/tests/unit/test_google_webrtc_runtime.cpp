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
#include <unordered_set>
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

struct PreviewResult {
    std::mutex mutex;
    std::condition_variable changed;
    size_t frames = 0;
    int width = 0;
    int height = 0;
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

TEST(GoogleWebRtcRuntime, EnumeratesNativeAudioDevicesWithOpaqueUniqueIds)
{
    driscord::media::GoogleWebRtcRuntime runtime;
    const auto inputs = runtime.recording_devices();
    const auto outputs = runtime.playout_devices();
    if (inputs.empty() || outputs.empty()) {
        GTEST_SKIP() << "No native audio service/devices in this environment";
    }

    const auto verify = [](const auto& devices) {
        EXPECT_EQ(devices.front().id, "default");
        std::unordered_set<std::string> ids;
        for (const auto& device : devices) {
            EXPECT_FALSE(device.id.empty());
            EXPECT_FALSE(device.name.empty());
            EXPECT_TRUE(ids.insert(device.id).second) << device.id;
        }
    };
    verify(inputs);
    verify(outputs);

    EXPECT_TRUE(runtime.set_recording_device("default"));
    EXPECT_TRUE(runtime.set_playout_device("default"));
    if (inputs.size() > 1) {
        EXPECT_TRUE(runtime.set_recording_device(inputs[1].id));
        EXPECT_TRUE(runtime.set_recording_device("default"));
    }
    if (outputs.size() > 1) {
        EXPECT_TRUE(runtime.set_playout_device(outputs[1].id));
        EXPECT_TRUE(runtime.set_playout_device("default"));
    }
    EXPECT_FALSE(runtime.set_recording_device("missing-device"));
    EXPECT_FALSE(runtime.set_playout_device("missing-device"));
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

TEST(GoogleWebRtcScreenSession, LocalPreviewCanBeDetachedWithoutStoppingShare)
{
    using namespace std::chrono_literals;
    using driscord::media::GoogleWebRtcScreenSession;

    driscord::media::GoogleWebRtcRuntime runtime;
    auto preview = std::make_shared<PreviewResult>();
    driscord::media::ScreenSessionCallbacks callbacks;
    callbacks.on_local_video =
        [preview](driscord::media::DecodedVideoFrameView frame) {
            {
                std::scoped_lock lock(preview->mutex);
                ++preview->frames;
                preview->width = frame.width;
                preview->height = frame.height;
            }
            preview->changed.notify_all();
        };
    GoogleWebRtcScreenSession session(runtime,
        {
            .remote_stream_slots = 0,
            .sharing_enabled = true,
            .local_preview_enabled = true,
        },
        std::move(callbacks));

    ASSERT_TRUE(session.start());
    constexpr int kWidth = 64;
    constexpr int kHeight = 36;
    const std::vector<uint8_t> bgra(
        static_cast<size_t>(kWidth * kHeight * 4), 127);
    ASSERT_TRUE(session.submit_bgra_frame(
        bgra, kWidth, kHeight, kWidth * 4, 1'000'000));
    {
        std::unique_lock lock(preview->mutex);
        ASSERT_TRUE(preview->changed.wait_for(
            lock, 2s, [&] { return preview->frames == 1; }));
        EXPECT_EQ(preview->width, kWidth);
        EXPECT_EQ(preview->height, kHeight);
    }

    session.set_local_preview_enabled(false);
    EXPECT_TRUE(session.sharing_enabled());
    (void)session.submit_bgra_frame(
        bgra, kWidth, kHeight, kWidth * 4, 1'033'333);
    {
        std::scoped_lock lock(preview->mutex);
        EXPECT_EQ(preview->frames, 1u);
    }

    session.set_local_preview_enabled(true);
    ASSERT_TRUE(session.submit_bgra_frame(
        bgra, kWidth, kHeight, kWidth * 4, 2'000'000));
    {
        std::unique_lock lock(preview->mutex);
        EXPECT_TRUE(preview->changed.wait_for(
            lock, 2s, [&] { return preview->frames == 2; }));
    }
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
