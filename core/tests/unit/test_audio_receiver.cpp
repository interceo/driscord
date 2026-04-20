#include <gtest/gtest.h>

#include "audio/audio.hpp"
#include "audio/opus_codec.hpp"
#include "utils/protocol.hpp"
#include "utils/time.hpp"
#include "utils/vector_view.hpp"

#include <cstring>
#include <thread>
#include <vector>

// Build a wire-format audio packet: AudioHeader + Opus-encoded silence.
static std::vector<uint8_t> make_audio_packet(uint64_t seq, int channels)
{
    OpusEncode enc;
    EXPECT_TRUE(enc.init(opus::kSampleRate, channels, 64000, 2048 /*VOIP*/));

    std::vector<float> pcm(static_cast<size_t>(opus::kFrameSize) * channels, 0.0f);
    std::vector<uint8_t> opus_buf(opus::kMaxPacket);
    int opus_len = enc.encode(pcm.data(), opus::kFrameSize, opus_buf.data(), opus::kMaxPacket);
    EXPECT_GT(opus_len, 0);

    std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize + opus_len);
    protocol::AudioHeader hdr { .seq = seq, .sender_ts = utils::WallNow() };
    hdr.serialize(pkt.data());
    std::memcpy(pkt.data() + protocol::AudioHeader::kWireSize, opus_buf.data(), opus_len);
    return pkt;
}

// Regression: AudioReceiver::pop() with channels=1 used to write into an
// unallocated mono_buf_ (SIGSEGV) because the early-return guard read
// `channels_ < 1` instead of `channels_ == 1`.
TEST(AudioReceiver, MonoPopDoesNotCrash)
{
    AudioReceiver recv(1 /*jitter_ms*/, 1 /*channels*/);

    auto pkt = make_audio_packet(0, 1);
    recv.push_packet(utils::vector_view<const uint8_t>(pkt.data(), pkt.size()));

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Must not crash (SIGSEGV before the fix).
    EXPECT_NO_FATAL_FAILURE(recv.pop());
}

// After pushing one packet and priming, pop() must return a non-empty frame.
TEST(AudioReceiver, MonoPopReturnsSamples)
{
    AudioReceiver recv(1 /*jitter_ms*/, 1 /*channels*/);

    auto pkt = make_audio_packet(0, 1);
    recv.push_packet(utils::vector_view<const uint8_t>(pkt.data(), pkt.size()));

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    auto view = recv.pop();
    EXPECT_GT(view.size(), 0u) << "pop() returned no samples for a mono receiver";
}

// Push several packets sequentially — every pop() must return samples without crashing.
TEST(AudioReceiver, MonoPopMultiplePackets)
{
    AudioReceiver recv(1 /*jitter_ms*/, 1 /*channels*/);

    constexpr int N = 10;
    for (int i = 0; i < N; ++i) {
        auto pkt = make_audio_packet(i, 1);
        recv.push_packet(utils::vector_view<const uint8_t>(pkt.data(), pkt.size()));
    }

    // Adaptive delay bumps to ≥10ms once skew estimator has ≥8 samples.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    int got = 0;
    for (int i = 0; i < N + 2; ++i) {
        auto view = recv.pop();
        if (view.size() > 0)
            ++got;
    }
    EXPECT_GT(got, 0);
}

// Stereo receiver (channels=2) must also pop without crashing — sanity check
// that the mono_buf_ mixdown path works for channels > 1.
TEST(AudioReceiver, StereoPopDoesNotCrash)
{
    AudioReceiver recv(1 /*jitter_ms*/, 2 /*channels*/);

    auto pkt = make_audio_packet(0, 2);
    recv.push_packet(utils::vector_view<const uint8_t>(pkt.data(), pkt.size()));

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    EXPECT_NO_FATAL_FAILURE(recv.pop());
}
