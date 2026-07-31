// A/V synchronisation under realistic network conditions.
//
// The problem this whole subsystem exists to solve: a peer's audio and its
// screen video travel as two independent streams with completely different
// packet shapes — a 20 ms Opus frame is one small packet, a video frame is
// hundreds of chunks that must all arrive before it can be decoded. Video is
// therefore both later and more erratic than audio, and if each stream picks
// its own playout delay the two drift apart until the picture no longer
// matches the sound.
//
// These tests drive both streams through a simulated network and assert on the
// only thing that matters: media captured at the same instant is played at the
// same instant.

#include "audio/audio.hpp"
#include "audio/opus_codec.hpp"
#include "config.hpp"
#include "net_cond.hpp"
#include "sync/media_clock.hpp"
#include "utils/log.hpp"
#include "utils/mono_clock.hpp"
#include "utils/protocol.hpp"
#include "video/video.hpp"
#include "video/video_codec.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

using avsync::MediaClock;
using Stream = avsync::MediaClock::Stream;

namespace {

constexpr int64_t kAudioPeriodUs = 20'000; // one Opus frame
constexpr int64_t kVideoPeriodUs = 16'667; // 60 fps

// Perceptual lip-sync tolerance. ITU-R BT.1359 puts the audible window at
// roughly +45 ms (audio ahead) to -125 ms (audio behind). In this file
// negative offsets mean audio is ahead of video.
constexpr int64_t kAudioAheadToleranceUs = 45'000;
constexpr int64_t kAudioBehindToleranceUs = 125'000;

// Replays a scripted stream of arrivals into a MediaClock. All time is
// simulated, so the tests are deterministic and take no wall-clock time.
struct StreamSim {
    MediaClock& clock;
    Stream stream;
    int64_t period_us;
    int64_t base_transit_us; // structural delay of this stream
    int64_t jitter_us;
    std::mt19937 rng;

    int64_t sender_ts_us = 0;
    int64_t last_local_us = 0;

    // Delivers `count` units and returns the local time of the last arrival.
    void run(int count)
    {
        for (int i = 0; i < count; ++i) {
            const int64_t j = jitter_us > 0
                ? static_cast<int64_t>(rng() % static_cast<uint64_t>(jitter_us))
                : 0;
            last_local_us = sender_ts_us + base_transit_us + j;
            clock.observe(stream, sender_ts_us, last_local_us);
            sender_ts_us += period_us;
        }
    }
};

} // namespace

// The core guarantee. Audio and video arrive with very different delays and
// very different jitter, and are still due at the same moment.
TEST(AvSync, StreamsWithDifferentDelaysSharePlayoutInstant)
{
    MediaClock clock;
    clock.set_video_active(true);

    StreamSim audio { clock, Stream::Audio, kAudioPeriodUs, 12'000, 6'000, std::mt19937(1) };
    StreamSim video { clock, Stream::Video, kVideoPeriodUs, 95'000, 45'000, std::mt19937(2) };

    audio.run(500);
    video.run(500);
    ASSERT_TRUE(clock.ready());

    // The shared delay has to cover the slower stream, or video would always
    // be shown late relative to its own audio.
    EXPECT_GT(clock.target_delay_us(), 80'000)
        << "delay does not cover video's structural lag behind audio";

    // The delay is not simply the sum of both streams' needs either — that
    // would trade lip sync for latency nobody asked for.
    EXPECT_LT(clock.target_delay_us(), 250'000);
}

// Relative timing within a stream must be preserved exactly: two frames 80 ms
// apart at capture are 80 ms apart on screen.
TEST(AvSync, RelativeTimingIsPreserved)
{
    MediaClock clock;
    clock.set_video_active(true);

    StreamSim audio { clock, Stream::Audio, kAudioPeriodUs, 12'000, 8'000, std::mt19937(3) };
    StreamSim video { clock, Stream::Video, kVideoPeriodUs, 90'000, 40'000, std::mt19937(4) };
    audio.run(400);
    video.run(400);

    const int64_t t0 = 2'000'000;
    for (int64_t offset : { 20'000, 80'000, 1'000'000 }) {
        EXPECT_EQ(clock.deadline_us(t0 + offset) - clock.deadline_us(t0), offset);
    }
}

// The condition the old implementation could not hold: as the network changes,
// the two streams must move together rather than each chasing its own delay.
TEST(AvSync, StaysLockedAcrossChangingConditions)
{
    MediaClock clock;
    clock.set_video_active(true);

    StreamSim audio { clock, Stream::Audio, kAudioPeriodUs, 10'000, 4'000, std::mt19937(5) };
    StreamSim video { clock, Stream::Video, kVideoPeriodUs, 70'000, 20'000, std::mt19937(6) };

    // Calm.
    audio.run(200);
    video.run(200);
    const int64_t calm_target = clock.target_delay_us();

    // The link degrades badly.
    audio.jitter_us = 90'000;
    video.jitter_us = 160'000;
    audio.run(200);
    video.run(200);
    const int64_t stormy_target = clock.target_delay_us();

    // And recovers.
    audio.jitter_us = 4'000;
    video.jitter_us = 20'000;
    audio.run(600);
    video.run(600);

    EXPECT_GT(stormy_target, calm_target + 50'000)
        << "did not buy headroom when the link got worse";
    EXPECT_LE(clock.target_delay_us(), stormy_target) << "delay never came back down";

    // Whatever the delay did, both streams moved with it: a frame and the
    // audio captured alongside it are still due together.
    EXPECT_EQ(clock.deadline_us(1'000'000) - clock.deadline_us(1'000'000 - 33'000), 33'000);
}

// Voice-only calls must not pay video's buffering cost. This is why video
// counts towards the shared delay only while its stream is being watched.
TEST(AvSync, VoiceOnlyCallKeepsLowLatency)
{
    MediaClock clock; // video_active stays false

    StreamSim audio { clock, Stream::Audio, kAudioPeriodUs, 10'000, 5'000, std::mt19937(7) };
    StreamSim video { clock, Stream::Video, kVideoPeriodUs, 120'000, 60'000, std::mt19937(8) };
    audio.run(400);
    video.run(400);

    const int64_t voice_only = clock.target_delay_us();
    EXPECT_LT(voice_only, sync_defaults::kMinDelayUs + 20'000)
        << "voice latency inflated by a stream nobody is watching";

    // The user opens the screen share.
    clock.set_video_active(true);
    audio.run(50);
    EXPECT_GT(clock.target_delay_us(), voice_only);
    EXPECT_GT(clock.target_delay_us(), 100'000);

    // And closes it again: latency returns, gradually.
    clock.set_video_active(false);
    audio.run(3000);
    EXPECT_LT(clock.target_delay_us(), sync_defaults::kMinDelayUs + 20'000)
        << "never returned to conversational latency";
}

// The measured delay must reflect the network, not the difference between two
// machines' clocks — which is arbitrary and can be hours.
TEST(AvSync, ClockOffsetBetweenMachinesDoesNotAffectDelay)
{
    const auto measure = [](int64_t offset_us) {
        MediaClock clock;
        clock.set_video_active(true);
        StreamSim audio { clock, Stream::Audio, kAudioPeriodUs,
            10'000 + offset_us, 8'000, std::mt19937(9) };
        StreamSim video { clock, Stream::Video, kVideoPeriodUs,
            80'000 + offset_us, 30'000, std::mt19937(10) };
        audio.run(400);
        video.run(400);
        return clock.target_delay_us();
    };

    const int64_t aligned = measure(0);
    EXPECT_EQ(measure(3'600'000'000), aligned); // peer's clock an hour ahead
    EXPECT_EQ(measure(-3'600'000'000), aligned); // and an hour behind
}

// A peer restarting resets its monotonic clock to zero. Everything measured
// against the old timeline is meaningless and must be dropped, not averaged in.
TEST(AvSync, PeerRestartResetsTheTimeline)
{
    MediaClock clock;
    clock.set_video_active(true);

    StreamSim audio { clock, Stream::Audio, kAudioPeriodUs, 10'000, 40'000, std::mt19937(11) };
    audio.run(400);
    ASSERT_TRUE(clock.ready());

    EXPECT_TRUE(clock.observe(Stream::Audio, 0, audio.last_local_us + 1000));
    EXPECT_FALSE(clock.ready());

    // It comes back on a fresh timeline and converges again.
    StreamSim fresh { clock, Stream::Audio, kAudioPeriodUs, 15'000, 5'000, std::mt19937(12) };
    fresh.run(400);
    EXPECT_TRUE(clock.ready());
    // Within one jitter sample of the new stream's transit time — the minimum
    // is whatever the luckiest packet saw, not an exact constant.
    EXPECT_NEAR(clock.offset_us(), 15'000, 5'000);
}

// Loss, reordering and duplication are the reorder buffer's problem, not the
// clock's — but they must not perturb the delay estimate either.
TEST(AvSync, ImpairedDeliveryDoesNotSkewTheEstimate)
{
    using test_util::NetProfile;

    MediaClock clock;
    clock.set_video_active(true);

    const auto profile = NetProfile::bad();
    std::mt19937 rng(13);

    StreamSim audio { clock, Stream::Audio, kAudioPeriodUs,
        static_cast<int64_t>(profile.delay_ms) * 1000,
        static_cast<int64_t>(profile.jitter_ms) * 1000, std::mt19937(14) };

    // Deliver with the profile's loss rate: dropped packets simply never call
    // observe(), which must not bias what the survivors report.
    for (int i = 0; i < 600; ++i) {
        const bool lost = (rng() % 100) < static_cast<uint32_t>(profile.loss_pct);
        if (lost) {
            audio.sender_ts_us += audio.period_us;
            continue;
        }
        audio.run(1);
    }

    ASSERT_TRUE(clock.ready());
    // The offset tracks the profile's base delay; loss removes samples but
    // must not bias what the survivors report.
    EXPECT_NEAR(clock.offset_us(), static_cast<int64_t>(profile.delay_ms) * 1000, 2'000);

    // The target should track the profile's jitter, not its base delay.
    EXPECT_GE(clock.target_delay_us(), sync_defaults::kMinDelayUs);
    EXPECT_LT(clock.target_delay_us(), 140'000);
}

// ---------------------------------------------------------------------------
// End to end: real codecs, real time, both receivers on one clock.
//
// Everything above reasons about the clock. This drives the actual playout —
// Opus decoding on one side, H.264 on the other — and measures where each
// stream really ends up, which is the only claim that matters.
// ---------------------------------------------------------------------------

namespace {

std::vector<uint8_t> encode_audio_packet(OpusEncode& enc,
    uint32_t seq,
    int64_t sender_ts_us,
    double& phase)
{
    std::vector<float> pcm(opus::kFrameSize);
    for (int i = 0; i < opus::kFrameSize; ++i) {
        pcm[static_cast<size_t>(i)] = static_cast<float>(0.3 * std::sin(phase));
        phase += 2.0 * std::numbers::pi * 220.0 / opus::kSampleRate;
    }

    std::vector<uint8_t> opus_buf(opus::kMaxPacket);
    const int len = enc.encode(pcm.data(), opus::kFrameSize, opus_buf.data(), opus::kMaxPacket);
    EXPECT_GT(len, 0);

    std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize + static_cast<size_t>(len));
    const protocol::AudioHeader hdr { .seq = seq, .flags = 0, .sender_ts_us = sender_ts_us };
    hdr.serialize(pkt.data());
    std::memcpy(pkt.data() + protocol::AudioHeader::kWireSize, opus_buf.data(),
        static_cast<size_t>(len));
    return pkt;
}

std::vector<uint8_t> wrap_video_packet(const std::vector<uint8_t>& encoded,
    int w, int h, int64_t sender_ts_us, protocol::VideoCodec codec)
{
    const protocol::VideoHeader vh {
        .width = static_cast<uint32_t>(w),
        .height = static_cast<uint32_t>(h),
        .sender_ts_us = sender_ts_us,
        .bitrate_kbps = 500,
        .frame_duration_us = static_cast<uint32_t>(kVideoPeriodUs),
        .flags = 0,
        .codec = codec,
    };
    std::vector<uint8_t> pkt(protocol::VideoHeader::kWireSize + encoded.size());
    vh.serialize(pkt.data());
    std::memcpy(pkt.data() + protocol::VideoHeader::kWireSize, encoded.data(), encoded.size());
    return pkt;
}

struct Scheduled {
    int64_t deliver_at_us = 0; // local time this packet reaches the receiver
    std::vector<uint8_t> bytes;
    uint64_t id = 0;
};

} // namespace

TEST(AvSync, ReceiversStayAlignedEndToEnd)
{
    driscord::set_min_log_level(driscord::LogLevel::None);

    constexpr int W = 160, H = 120;
    constexpr int kAudioFrames = 120; // 2.4 s
    constexpr int64_t kAudioTransitUs = 15'000;
    constexpr int64_t kVideoTransitUs = 85'000; // video is structurally later
    constexpr int64_t kAudioJitterUs = 8'000;
    constexpr int64_t kVideoJitterUs = 35'000;

    auto clock = std::make_shared<MediaClock>();
    AudioReceiver audio(clock, 1);
    VideoReceiver video("peer", clock);

    // Build the two streams up front so encoding cost stays out of the timed
    // loop, on one shared capture timeline.
    OpusEncode enc;
    ASSERT_TRUE(enc.init(opus::kSampleRate, 1, 64000, 2048 /*VOIP*/));

    VideoEncoder venc;
    ASSERT_TRUE(venc.init(W, H, 60, 500).has_value());
    std::vector<uint8_t> bgra(static_cast<size_t>(W) * H * 4, 96);

    std::mt19937 rng(4242);
    const int64_t start = utils::MonoClock::now_us() + 50'000;

    std::vector<Scheduled> audio_pkts;
    double phase = 0.0;
    for (int i = 0; i < kAudioFrames; ++i) {
        const int64_t capture = start + i * kAudioPeriodUs;
        audio_pkts.push_back(Scheduled {
            .deliver_at_us = capture + kAudioTransitUs
                + static_cast<int64_t>(rng() % kAudioJitterUs),
            .bytes = encode_audio_packet(enc, static_cast<uint32_t>(i), capture, phase),
            .id = static_cast<uint64_t>(i),
        });
    }

    std::vector<Scheduled> video_pkts;
    const int64_t video_span = kAudioFrames * kAudioPeriodUs;
    for (int i = 0; i * kVideoPeriodUs < video_span; ++i) {
        const auto& out = venc.encode(bgra, W, H);
        if (out.empty()) {
            continue;
        }
        const int64_t capture = start + i * kVideoPeriodUs;
        video_pkts.push_back(Scheduled {
            .deliver_at_us = capture + kVideoTransitUs
                + static_cast<int64_t>(rng() % kVideoJitterUs),
            .bytes = wrap_video_packet(out, W, H, capture, venc.codec()),
            .id = static_cast<uint64_t>(i),
        });
    }
    ASSERT_GT(video_pkts.size(), 30u) << "encoder produced too little to measure";

    // Drive the receivers in real time: deliver packets when they are due,
    // pull audio at the device period, and render as fast as the UI would.
    std::vector<float> out(opus::kFrameSize);
    size_t next_audio = 0, next_video = 0;
    int64_t last_frame_ts = -1;

    // Stand-in for the sound card. A real device consumes samples at a fixed
    // rate; this pulls however many real time says are due, so the test does
    // not manufacture a drift the playout would then have to correct.
    const int64_t device_start = utils::MonoClock::now_us();
    int64_t frames_consumed = 0;

    std::vector<int64_t> errors;
    const int64_t deadline = start + video_span + 400'000;

    while (utils::MonoClock::now_us() < deadline) {
        const int64_t now = utils::MonoClock::now_us();

        while (next_audio < audio_pkts.size()
            && audio_pkts[next_audio].deliver_at_us <= now) {
            const auto& p = audio_pkts[next_audio++];
            audio.push_packet(utils::vector_view<const uint8_t>(p.bytes.data(), p.bytes.size()));
        }
        while (next_video < video_pkts.size()
            && video_pkts[next_video].deliver_at_us <= now) {
            const auto& p = video_pkts[next_video++];
            video.push_video_packet(
                utils::vector_view<const uint8_t>(p.bytes.data(), p.bytes.size()), p.id);
        }

        const int64_t frames_due = (now - device_start) * opus::kSampleRate / 1'000'000;
        while (frames_consumed + opus::kFrameSize <= frames_due) {
            audio.read(out.data(), out.size());
            frames_consumed += opus::kFrameSize;
        }

        video.update([&](const VideoReceiver::Frame& f) { last_frame_ts = f.sender_ts_us; });

        // Once both streams are running, the picture on screen and the audio
        // in the speaker should be describing the same moment.
        // Sample steady state only. Start-up is excluded because the shared
        // delay is still growing to cover video, and the end is excluded
        // because video runs out first and the last frame simply stays on
        // screen while the audio buffer drains — neither says anything about
        // whether the two streams track each other.
        const int64_t playout = audio.stats().playout_ts_us;
        if (last_frame_ts >= 0 && clock->ready()
            && playout > start + 1'200'000
            && playout < start + video_span - 300'000) {
            errors.push_back(last_frame_ts - playout);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_GT(errors.size(), 50u) << "not enough samples to judge";

    std::sort(errors.begin(), errors.end());
    const int64_t median = errors[errors.size() / 2];
    const int64_t p95 = errors[errors.size() * 95 / 100];
    const int64_t p05 = errors[errors.size() * 5 / 100];

    const auto report = [&](const char* what, int64_t v) {
        return std::string(what) + " " + std::to_string(v / 1000) + " ms"
            + " (median " + std::to_string(median / 1000)
            + ", p05 " + std::to_string(p05 / 1000)
            + ", p95 " + std::to_string(p95 / 1000)
            + ", n=" + std::to_string(errors.size()) + ")";
    };

    auto expect_lip_sync = [&](const char* what, int64_t v, int64_t scale = 1) {
        if (v < 0) {
            EXPECT_LT(-v, kAudioAheadToleranceUs * scale) << report(what, v);
        } else {
            EXPECT_LT(v, kAudioBehindToleranceUs * scale) << report(what, v);
        }
    };

    expect_lip_sync("median A/V offset", median);
    expect_lip_sync("p95 A/V offset", p95, 2);
    expect_lip_sync("p05 A/V offset", p05, 2);

    // And the streams actually ran, rather than both sitting silent.
    EXPECT_GT(audio.stats().packets_received, 80u);
    EXPECT_GT(video.video_stats().packets_received, 30u);
}
