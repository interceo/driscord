
#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"
#include "webrtc/google_webrtc_voice_session.hpp"

#include "test/rtp_file_reader.h"
#include "test/rtp_file_writer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using test_util::SignalingServerFixture;
using test_util::Waiter;

namespace {

struct CapturedPacket {
    std::vector<uint8_t> bytes;
    uint32_t time_ms = 0;
};

class PacketRecorder {
public:
    void observe(const rtc::binary& packet)
    {
        if (rtc::IsRtcp(packet)) {
            return;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        CapturedPacket captured;
        captured.bytes.resize(packet.size());
        std::memcpy(captured.bytes.data(), packet.data(), packet.size());
        captured.time_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                .count());
        std::scoped_lock lock(mutex_);
        packets_.push_back(std::move(captured));
    }

    size_t count() const
    {
        std::scoped_lock lock(mutex_);
        return packets_.size();
    }

    bool wait_for_count(size_t minimum,
        std::chrono::milliseconds timeout = test_util::kDefaultTimeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (count() < minimum) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    }

    std::vector<CapturedPacket> snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return packets_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<CapturedPacket> packets_;
    std::chrono::steady_clock::time_point start_
        = std::chrono::steady_clock::now();
};

void write_rtpdump(const std::string& path,
    const std::vector<CapturedPacket>& packets)
{
    std::unique_ptr<webrtc::test::RtpFileWriter> writer(
        webrtc::test::RtpFileWriter::Create(
            webrtc::test::RtpFileWriter::kRtpDump, path));
    ASSERT_NE(writer, nullptr) << "cannot create " << path;
    for (const auto& captured : packets) {
        webrtc::test::RtpPacket packet;
        ASSERT_LE(captured.bytes.size(), sizeof(packet.data));
        std::memcpy(packet.data, captured.bytes.data(), captured.bytes.size());
        packet.length = captured.bytes.size();
        packet.original_length = captured.bytes.size();
        packet.time_ms = captured.time_ms;
        ASSERT_TRUE(writer->WritePacket(&packet));
    }
}

void verify_capture(const std::string& path, size_t expected_minimum)
{
    std::unique_ptr<webrtc::test::RtpFileReader> reader(
        webrtc::test::RtpFileReader::Create(
            webrtc::test::RtpFileReader::kRtpDump, path));
    ASSERT_NE(reader, nullptr);

    std::map<uint32_t, std::vector<uint16_t>> sequences_by_ssrc;
    size_t total = 0;
    webrtc::test::RtpPacket packet;
    while (reader->NextPacket(&packet)) {
        ASSERT_GE(packet.length, 12u);
        ASSERT_EQ(packet.data[0] >> 6, 2) << "not RTP v2 at packet " << total;
        const uint32_t ssrc = (uint32_t(packet.data[8]) << 24)
            | (uint32_t(packet.data[9]) << 16) | (uint32_t(packet.data[10]) << 8)
            | packet.data[11];
        const uint16_t sequence
            = uint16_t((packet.data[2] << 8) | packet.data[3]);
        sequences_by_ssrc[ssrc].push_back(sequence);
        ++total;
    }
    ASSERT_GE(total, expected_minimum);

    const auto dominant = std::max_element(sequences_by_ssrc.begin(),
        sequences_by_ssrc.end(), [](const auto& a, const auto& b) {
            return a.second.size() < b.second.size();
        });
    ASSERT_NE(dominant, sequences_by_ssrc.end());
    const auto& sequences = dominant->second;
    ASSERT_GE(sequences.size(), expected_minimum);
    for (size_t i = 1; i < sequences.size(); ++i) {
        ASSERT_EQ(uint16_t(sequences[i - 1] + 1), sequences[i])
            << "sequence gap on the tap side at packet " << i
            << " — the tap runs before any injected fault, so loopback "
               "capture must be gap-free";
    }
}

void maybe_record_fixtures(const std::string& name,
    const std::vector<CapturedPacket>& packets)
{
    const char* dir = std::getenv("DRISCORD_RECORD_FIXTURES_DIR");
    if (dir == nullptr || *dir == '\0') {
        return;
    }
    std::filesystem::create_directories(dir);
    write_rtpdump(std::string(dir) + "/" + name + ".rtpdump", packets);

    std::set<size_t> seed_sizes;
    size_t seed_index = 0;
    for (const auto& captured : packets) {
        if (!seed_sizes.insert(captured.bytes.size()).second) {
            continue;
        }
        if (++seed_index > 6) {
            break;
        }
        std::ofstream seed(std::string(dir) + "/" + name + "_seed_"
                + std::to_string(seed_index) + ".bin",
            std::ios::binary);
        seed.put(char(0x80));
        seed.put(char(0x03));
        seed.write(reinterpret_cast<const char*>(captured.bytes.data()),
            static_cast<std::streamsize>(captured.bytes.size()));
    }
}

std::vector<int16_t> make_tone_frame(double frequency_hz, size_t frame_index)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kSampleRate = 48'000;
    const size_t samples_per_frame = kSampleRate / 100;
    std::vector<int16_t> result(samples_per_frame);
    const size_t offset = frame_index * samples_per_frame;
    for (size_t i = 0; i < samples_per_frame; ++i) {
        result[i] = static_cast<int16_t>(std::lround(12'000.0
            * std::sin(2.0 * kPi * frequency_hz
                * static_cast<double>(offset + i) / kSampleRate)));
    }
    return result;
}

std::vector<uint8_t> make_bgra_frame(int width, int height, size_t frame_index)
{
    std::vector<uint8_t> image(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (size_t i = 0; i < image.size(); i += 4) {
        image[i] = static_cast<uint8_t>((i + frame_index * 7) & 0xff);
        image[i + 1] = static_cast<uint8_t>(frame_index * 3);
        image[i + 2] = 0x40;
        image[i + 3] = 0xff;
    }
    return image;
}

TEST(RtpDumpCapture, VoicePublisherRtpRoundTripsThroughRtpdump)
{
    PacketRecorder recorder;
    SignalingServerFixture server { driscord::sfu::RtpFaultConfig {
        .packet_tap =
            [&recorder](const rtc::binary& packet) {
                recorder.observe(packet);
            },
    } };

    const driscord::media::GoogleWebRtcRuntimeConfig runtime_config {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
    driscord::media::GoogleWebRtcRuntime runtime(runtime_config);
    Transport transport;
    Waiter connected;

    driscord::media::VoiceSessionCallbacks callbacks;
    callbacks.on_offer = [&transport](std::string sdp) {
        transport.send_media_offer(signaling::ConnectionId::Voice, sdp);
    };
    callbacks.on_candidate = [&transport](
                                 std::string candidate, std::string mid) {
        transport.send_media_candidate(
            signaling::ConnectionId::Voice, candidate, mid);
    };
    callbacks.on_state
        = [&connected](driscord::media::VoiceConnectionState state) {
              if (state == driscord::media::VoiceConnectionState::Connected) {
                  connected.signal();
              }
          };
    driscord::media::GoogleWebRtcVoiceSession voice(runtime,
        { .remote_track_slots = 2, .microphone_enabled = true },
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

    for (size_t frame = 0; frame < 150; ++frame) {
        ASSERT_TRUE(runtime.submit_recorded_audio_10ms(
            make_tone_frame(440.0, frame)));
    }
    ASSERT_TRUE(recorder.wait_for_count(60));

    voice.close();
    transport.disconnect();

    const std::string path
        = ::testing::TempDir() + "driscord_capture_voice.rtpdump";
    const auto packets = recorder.snapshot();
    write_rtpdump(path, packets);
    verify_capture(path, 60);
    maybe_record_fixtures("voice", packets);
    std::filesystem::remove(path);
}

TEST(RtpDumpCapture, ScreenPublisherRtpRoundTripsThroughRtpdump)
{
    PacketRecorder recorder;
    SignalingServerFixture server { driscord::sfu::RtpFaultConfig {
        .packet_tap =
            [&recorder](const rtc::binary& packet) {
                recorder.observe(packet);
            },
    } };

    const driscord::media::GoogleWebRtcRuntimeConfig runtime_config {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
    driscord::media::GoogleWebRtcRuntime runtime(runtime_config);
    Transport transport;
    Waiter connected;
    using driscord::media::ScreenConnectionState;

    driscord::media::ScreenSessionCallbacks callbacks;
    callbacks.on_offer = [&transport](std::string sdp) {
        transport.send_media_offer(signaling::ConnectionId::Screen, sdp);
    };
    callbacks.on_candidate = [&transport](
                                 std::string candidate, std::string mid) {
        transport.send_media_candidate(
            signaling::ConnectionId::Screen, candidate, mid);
    };
    callbacks.on_state = [&connected](ScreenConnectionState state) {
        if (state == ScreenConnectionState::Connected) {
            connected.signal();
        }
    };
    driscord::media::GoogleWebRtcScreenSession screen(runtime,
        {
            .remote_stream_slots = 1,
            .sharing_enabled = true,
            .system_audio_enabled = false,
            .max_video_bitrate_bps = 300'000,
        },
        std::move(callbacks));
    transport.on_media_answer(
        [&screen](signaling::ConnectionId connection, const std::string& sdp) {
            if (connection == signaling::ConnectionId::Screen) {
                screen.apply_answer(sdp);
            }
        });
    transport.on_media_candidate([&screen](signaling::ConnectionId connection,
                                     const std::string& candidate,
                                     const std::string& mid) {
        if (connection == signaling::ConnectionId::Screen) {
            screen.add_remote_candidate(candidate, mid);
        }
    });

    ASSERT_TRUE(transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(transport));
    ASSERT_TRUE(screen.start());
    ASSERT_TRUE(connected.wait_for());
    transport.send_streaming_start();

    constexpr int kWidth = 320;
    constexpr int kHeight = 180;
    const auto epoch = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t start_us
        = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
    for (size_t frame = 0; frame < 150 && recorder.count() < 120; ++frame) {
        const auto image = make_bgra_frame(kWidth, kHeight, frame);
        (void)screen.submit_bgra_frame(image, kWidth, kHeight, kWidth * 4,
            start_us + static_cast<int64_t>(frame) * 33'333);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    ASSERT_TRUE(recorder.wait_for_count(120));

    screen.close();
    transport.disconnect();

    const std::string path
        = ::testing::TempDir() + "driscord_capture_screen.rtpdump";
    const auto packets = recorder.snapshot();
    write_rtpdump(path, packets);
    verify_capture(path, 60);
    maybe_record_fixtures("screen", packets);
    std::filesystem::remove(path);
}

}
