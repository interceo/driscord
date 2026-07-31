#include <gtest/gtest.h>

#include "audio/audio.hpp"
#include "audio/opus_codec.hpp"
#include "sync/media_clock.hpp"
#include "utils/mono_clock.hpp"
#include "utils/protocol.hpp"
#include "utils/vector_view.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

namespace {

constexpr int64_t kFrameUs = 20'000; // one Opus frame

// Encodes a tone rather than silence: DTX would turn silence into packets that
// carry almost nothing, which is not what these tests are exercising.
std::vector<uint8_t> make_packet(uint32_t seq,
    int64_t sender_ts_us,
    int channels,
    uint32_t flags = 0)
{
    static thread_local double phase = 0.0;

    OpusEncode enc;
    EXPECT_TRUE(enc.init(opus::kSampleRate, channels, 64000, 2048 /*VOIP*/));

    std::vector<float> pcm(static_cast<size_t>(opus::kFrameSize) * static_cast<size_t>(channels));
    for (int i = 0; i < opus::kFrameSize; ++i) {
        const float s = static_cast<float>(0.3 * std::sin(phase));
        phase += 2.0 * std::numbers::pi * 220.0 / opus::kSampleRate;
        for (int c = 0; c < channels; ++c) {
            pcm[static_cast<size_t>(i) * static_cast<size_t>(channels)
                + static_cast<size_t>(c)]
                = s;
        }
    }

    std::vector<uint8_t> opus_buf(opus::kMaxPacket);
    const int len = enc.encode(pcm.data(), opus::kFrameSize, opus_buf.data(), opus::kMaxPacket);
    EXPECT_GT(len, 0);

    std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize + static_cast<size_t>(len));
    const protocol::AudioHeader hdr {
        .seq = seq,
        .flags = flags,
        .sender_ts_us = sender_ts_us,
    };
    hdr.serialize(pkt.data());
    std::memcpy(pkt.data() + protocol::AudioHeader::kWireSize, opus_buf.data(),
        static_cast<size_t>(len));
    return pkt;
}

void push(AudioReceiver& recv, const std::vector<uint8_t>& pkt)
{
    recv.push_packet(utils::vector_view<const uint8_t>(pkt.data(), pkt.size()));
}

// Drives the receiver at true wall-clock pace: one 20 ms packet delivered and
// one 20 ms block consumed per step.
//
// Real time is not incidental here. The playout exists to place audio on a
// schedule, so a test that asks for a second of audio in a microsecond is
// asking it to do the one thing it must refuse. Stepping in real time is what
// makes the assertions below mean anything.
struct Pacer {
    AudioReceiver& recv;
    int channels = 1;
    uint32_t seq = 0;
    size_t block = opus::kFrameSize;
    std::vector<float> buf = std::vector<float>(opus::kFrameSize);
    size_t real_samples = 0;

    // One step: optionally deliver this packet, wait a frame, consume a block.
    void step(bool deliver = true, uint32_t flags = 0)
    {
        if (deliver) {
            push(recv, make_packet(seq, utils::MonoClock::now_us(), channels, flags));
        }
        ++seq;
        std::this_thread::sleep_for(std::chrono::microseconds(kFrameUs));
        real_samples += recv.read(buf.data(), block);
    }

    void run(int steps, uint32_t flags = 0)
    {
        for (int i = 0; i < steps; ++i) {
            step(true, flags);
        }
    }

    // Steps during which nothing arrives.
    void starve(int steps)
    {
        for (int i = 0; i < steps; ++i) {
            step(false);
        }
    }

    // Steps during which the packet is generated but lost in transit.
    void lose(int count)
    {
        for (int i = 0; i < count; ++i) {
            step(false);
        }
    }
};

std::shared_ptr<avsync::MediaClock> make_clock() { return std::make_shared<avsync::MediaClock>(); }

} // namespace

TEST(AudioReceiver, MonoReadDoesNotCrash)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    EXPECT_NO_FATAL_FAILURE(p.run(3));
}

TEST(AudioReceiver, StereoReadDoesNotCrash)
{
    AudioReceiver recv(make_clock(), 2);
    Pacer p { recv, 2 };
    EXPECT_NO_FATAL_FAILURE(p.run(3));
}

TEST(AudioReceiver, ProducesSamplesForDeliveredPackets)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    // The playout holds until the clock has enough arrivals to place it, so a
    // stream has to run a little longer than that before audio comes out.
    p.run(45);

    EXPECT_GT(p.real_samples, 15u * opus::kFrameSize)
        << "most of a cleanly delivered stream should reach the mixer";
    EXPECT_EQ(recv.stats().packets_received, 45u);
    EXPECT_EQ(recv.stats().drop_count, 0u);
}

// An empty stream must produce silence, not garbage, and must not report the
// silence as real audio.
TEST(AudioReceiver, EmptyStreamReadsSilence)
{
    AudioReceiver recv(make_clock(), 1);
    std::vector<float> buf(opus::kFrameSize, 1.0f);
    EXPECT_EQ(recv.read(buf.data(), buf.size()), 0u);
    for (float v : buf) {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }
}

// The device period is not guaranteed to match the Opus frame size, and the
// two do not divide evenly. Whatever block size is asked for, decoded samples
// may not be dropped on the floor — under the old fixed-size path the tail of
// every frame was silently discarded.
TEST(AudioReceiver, OddBlockSizesLoseNoSamples)
{
    for (size_t block : { 128u, 441u, 700u, 1024u }) {
        AudioReceiver recv(make_clock(), 1);
        Pacer p { recv, 1 };
        p.block = block;
        p.buf.assign(block, 0.0f);

        // Deliver 25 frames while consuming `block` samples every 20 ms. Fewer
        // samples leave than arrive when block < 960, so measure against what
        // was actually asked for rather than what was sent.
        p.run(45);

        const size_t asked = 25 * block;
        EXPECT_GT(p.real_samples, asked * 7 / 10) << "block=" << block;
        EXPECT_EQ(recv.stats().decode_errors, 0u) << "block=" << block;
    }
}

// A hole with the very next packet already in hand is recoverable from the
// FEC copy Opus embeds in it. The encoder has always paid for those bits.
TEST(AudioReceiver, RecoversLostPacketFromNextPacketFec)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(25);
    p.lose(1); // this packet never arrives...
    p.run(15); // ...but the one after it does, and carries FEC for it

    EXPECT_GT(recv.stats().fec_count, 0u) << "in-band FEC was available but not used";
}

// A run of losses leaves no FEC to draw on, so concealment has to cover it.
TEST(AudioReceiver, ConcealsWhenFecIsUnavailable)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(25);
    p.lose(6);
    p.run(15);

    EXPECT_GT(recv.stats().conceal_count, 0u);
}

TEST(AudioReceiver, HandlesReorderedArrival)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(20);

    // Two packets swap places in flight.
    const auto later = make_packet(p.seq + 1, utils::MonoClock::now_us(), 1);
    const auto earlier = make_packet(p.seq, utils::MonoClock::now_us(), 1);
    push(recv, later);
    push(recv, earlier);
    p.seq += 2;
    std::this_thread::sleep_for(std::chrono::microseconds(kFrameUs));
    recv.read(p.buf.data(), p.block);

    p.run(8);

    const auto st = recv.stats();
    EXPECT_EQ(st.packets_received, 30u);
    EXPECT_EQ(st.drop_count, 0u) << "reordered packets must not be discarded";
}

TEST(AudioReceiver, DropsPacketsThatArriveTooLate)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(30);

    // A straggler from the very start of the call, long since played past.
    push(recv, make_packet(1, utils::MonoClock::now_us(), 1));
    EXPECT_GT(recv.stats().drop_count, 0u);
}

TEST(AudioReceiver, DropsOversizedPackets)
{
    AudioReceiver recv(make_clock(), 1);
    std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize + 4000, 0);
    const protocol::AudioHeader hdr { .seq = 0, .flags = 0, .sender_ts_us = 0 };
    hdr.serialize(pkt.data());
    push(recv, pkt);

    const auto st = recv.stats();
    EXPECT_EQ(st.drop_count, 1u);
    EXPECT_EQ(st.queue_size, 0u);
}

TEST(AudioReceiver, IgnoresTruncatedPackets)
{
    AudioReceiver recv(make_clock(), 1);
    std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize, 0);
    push(recv, pkt);
    EXPECT_EQ(recv.stats().packets_received, 0u);
}

// The sender stops transmitting during silence, so a gap flagged as the start
// of a talkspurt is a pause the speaker took — not loss. Concealing across it
// would paint speech over the pause.
TEST(AudioReceiver, DoesNotConcealAcrossDeliberateSilence)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(25);

    p.starve(15); // 300 ms of silence, nothing sent
    const uint64_t during = recv.stats().conceal_count;

    p.run(20, protocol::flags::kTalkspurtStart);

    EXPECT_EQ(recv.stats().conceal_count, during)
        << "concealed across a pause the speaker actually took";
    EXPECT_GT(p.real_samples, 0u);
}

// Without the flag a pause is indistinguishable from loss, so concealment does
// run — but it must be bounded by how long the stream has been quiet, not by
// how big the hole is. The buffer this replaces would conceal indefinitely.
TEST(AudioReceiver, UnflaggedGapConcealmentIsBounded)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(25);

    const uint64_t before = recv.stats().conceal_count;
    p.starve(60); // 1.2 s of nothing

    EXPECT_LE(recv.stats().conceal_count - before, 25u)
        << "kept generating concealment frames for a stream that stopped";
}

// A stream that dies must come back cleanly when it resumes.
TEST(AudioReceiver, RecoversAfterStreamStops)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(25);
    p.starve(40);

    const size_t before = p.real_samples;
    p.run(30, protocol::flags::kTalkspurtStart);
    EXPECT_GT(p.real_samples, before) << "did not recover after the stream resumed";
}

TEST(AudioReceiver, ResetClearsState)
{
    AudioReceiver recv(make_clock(), 1);
    Pacer p { recv, 1 };
    p.run(25);
    ASSERT_GT(recv.stats().packets_received, 0u);

    recv.reset();
    p.step(false); // reset is consumed on the audio thread

    const auto st = recv.stats();
    EXPECT_EQ(st.packets_received, 0u);
    EXPECT_EQ(st.queue_size, 0u);

    const size_t before = p.real_samples;
    p.run(40);
    EXPECT_GT(p.real_samples, before) << "unusable after reset";
}

TEST(AudioReceiver, VolumeMuteAndPanRoundTrip)
{
    AudioReceiver recv(make_clock(), 1);
    recv.set_volume(0.25f);
    recv.set_muted(true);
    recv.set_pan(0.75f);
    EXPECT_FLOAT_EQ(recv.volume(), 0.25f);
    EXPECT_TRUE(recv.muted());
    EXPECT_FLOAT_EQ(recv.pan(), 0.75f);

    recv.set_pan(5.0f); // clamped into range
    EXPECT_FLOAT_EQ(recv.pan(), 1.0f);
}

// Both halves of a screen share hand the same clock to their receiver; the
// audio side must report arrivals against it and play to its schedule.
TEST(AudioReceiver, PlaysToItsSharedClock)
{
    auto c = make_clock();
    AudioReceiver recv(c, 1);
    Pacer p { recv, 1 };
    p.run(30);

    EXPECT_EQ(recv.clock(), c);
    ASSERT_TRUE(c->ready()) << "arrivals were not reported to the shared clock";

    const auto st = recv.stats();
    EXPECT_GT(st.target_delay_ms, 0);
    // A clean local link needs no more than the configured floor, and the
    // playout should be sitting close to it rather than drifting away.
    EXPECT_LT(std::llabs(st.actual_delay_ms - st.target_delay_ms), 150)
        << "playout drifted away from its target delay";
}
