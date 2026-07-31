#include <gtest/gtest.h>

#include "config.hpp"
#include "sync/media_clock.hpp"

#include <random>
#include <thread>

using avsync::MediaClock;
using Stream = avsync::MediaClock::Stream;

namespace {

constexpr int64_t kPacketUs = 20'000; // one Opus frame
constexpr int64_t kFrameUs = 16'667; // one video frame at 60 fps

// Drives `count` arrivals of a stream whose transit takes `transit_us` plus a
// per-packet jitter drawn from [0, jitter_us).
struct Feeder {
    MediaClock& clock;
    Stream stream;
    int64_t sender_ts_us = 0;
    int64_t local_now_us = 0;
    int64_t period_us = kPacketUs;
    int64_t transit_us = 0;
    int64_t clock_offset_us = 0;
    std::mt19937 rng { 42 };

    void run(int count, int64_t jitter_us = 0)
    {
        for (int i = 0; i < count; ++i) {
            const int64_t j = jitter_us > 0
                ? static_cast<int64_t>(rng() % static_cast<uint64_t>(jitter_us))
                : 0;
            local_now_us = sender_ts_us + clock_offset_us + transit_us + j;
            clock.observe(stream, sender_ts_us, local_now_us);
            sender_ts_us += period_us;
        }
    }
};

} // namespace

TEST(MediaClock, NotReadyBeforeAnyTraffic)
{
    MediaClock c;
    EXPECT_FALSE(c.ready());
    EXPECT_EQ(c.target_delay_us(), 0);
}

TEST(MediaClock, ConvergesOnCleanAudioLink)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 8'000;
    f.run(300);

    ASSERT_TRUE(c.ready());
    EXPECT_EQ(c.offset_us(), 8'000);
    // A jitter-free link only needs the configured floor.
    EXPECT_EQ(c.target_delay_us(), sync_defaults::kMinDelayUs);
}

TEST(MediaClock, DelayIsIndependentOfClockOffset)
{
    MediaClock a, b;

    Feeder fa { a, Stream::Audio };
    fa.transit_us = 8'000;
    fa.clock_offset_us = 0;
    fa.run(300, 12'000);

    Feeder fb { b, Stream::Audio };
    fb.transit_us = 8'000;
    fb.clock_offset_us = 6L * 3600 * 1'000'000; // peer's clock six hours ahead
    fb.run(300, 12'000);

    EXPECT_EQ(a.target_delay_us(), b.target_delay_us());
    EXPECT_NE(a.offset_us(), b.offset_us()); // the offset absorbs the difference
}

TEST(MediaClock, DeadlineFollowsSenderTimeline)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 10'000;
    f.run(300);
    ASSERT_TRUE(c.ready());

    const int64_t d0 = c.deadline_us(1'000'000);
    const int64_t d1 = c.deadline_us(1'020'000);
    // Media 20 ms apart at the sender is due 20 ms apart at the receiver.
    EXPECT_EQ(d1 - d0, 20'000);
    EXPECT_EQ(d0, 1'000'000 + c.offset_us() + c.target_delay_us());
}

// A jittery link must buy more headroom than a clean one.
TEST(MediaClock, JitterRaisesTarget)
{
    MediaClock clean, noisy;

    Feeder fc { clean, Stream::Audio };
    fc.transit_us = 10'000;
    fc.run(400);

    Feeder fn { noisy, Stream::Audio };
    fn.transit_us = 10'000;
    fn.run(400, 60'000);

    EXPECT_GT(noisy.target_delay_us(), clean.target_delay_us());
    EXPECT_GT(noisy.target_delay_us(), 40'000);
    EXPECT_LE(noisy.target_delay_us(), sync_defaults::kMaxDelayUs);
}

TEST(MediaClock, TargetIsClamped)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 1'000;
    f.run(600, 5'000'000); // absurd jitter
    EXPECT_EQ(c.target_delay_us(), sync_defaults::kMaxDelayUs);
}

// Video sits behind audio structurally: a frame cannot leave the sender until
// all of its chunks are out, so its one-way delay carries a constant excess.
// While the user is watching, audio has to wait for it; while they are not, it
// must not pay that cost.
TEST(MediaClock, VideoOnlyCountsWhileWatching)
{
    MediaClock c;

    Feeder audio { c, Stream::Audio };
    audio.transit_us = 10'000;
    audio.run(400, 4'000);

    Feeder video { c, Stream::Video };
    video.period_us = kFrameUs;
    video.transit_us = 90'000; // 80 ms behind audio
    video.run(400, 30'000);

    const int64_t audio_only = c.target_delay_us();
    EXPECT_LT(audio_only, sync_defaults::kMinDelayUs + 20'000)
        << "video must not inflate a voice-only call";

    c.set_video_active(true);
    audio.run(50, 4'000); // any arrival republishes the target
    const int64_t shared = c.target_delay_us();

    EXPECT_GT(shared, audio_only);
    EXPECT_GT(shared, 80'000) << "must cover video's structural extra delay";
}

TEST(MediaClock, BothStreamsShareOneDeadlineBasis)
{
    MediaClock c;
    c.set_video_active(true);

    Feeder audio { c, Stream::Audio };
    audio.transit_us = 10'000;
    audio.run(400, 5'000);

    Feeder video { c, Stream::Video };
    video.period_us = kFrameUs;
    video.transit_us = 60'000;
    video.run(400, 20'000);
    ASSERT_TRUE(c.ready());

    // Media captured at the same instant is due at the same instant, whichever
    // stream it came from — that is what keeps lips on the audio.
    constexpr int64_t kCapture = 5'000'000;
    EXPECT_EQ(c.deadline_us(kCapture), c.deadline_us(kCapture));

    // And the deadline for video captured 80 ms later is exactly 80 ms later.
    EXPECT_EQ(c.deadline_us(kCapture + 80'000) - c.deadline_us(kCapture), 80'000);
}

TEST(MediaClock, TargetRisesImmediatelyAndFallsSlowly)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 10'000;
    f.run(600);
    const int64_t calm = c.target_delay_us();

    // Network degrades: the target must jump now, an underrun is audible.
    f.run(200, 120'000);
    const int64_t stormy = c.target_delay_us();
    EXPECT_GT(stormy, calm + 40'000);

    // Network recovers. One well-behaved packet must not collapse the buffer.
    f.run(1, 0);
    EXPECT_GT(c.target_delay_us(), stormy - sync_defaults::kDelayDecayStepUs - 1);

    // It comes down over time, not at once.
    f.run(2000);
    EXPECT_LT(c.target_delay_us(), stormy);
}

TEST(MediaClock, SenderRestartResetsEstimates)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 10'000;
    f.run(400, 50'000);
    ASSERT_TRUE(c.ready());

    // The peer's process restarts: its monotonic clock goes back to zero.
    const bool restarted = c.observe(Stream::Audio, 0, f.local_now_us + 1000);
    EXPECT_TRUE(restarted);
    EXPECT_FALSE(c.ready());
    EXPECT_EQ(c.target_delay_us(), 0);
}

TEST(MediaClock, SmallBackwardJitterIsNotARestart)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 10'000;
    f.run(400);
    ASSERT_TRUE(c.ready());

    // Reordered packet: an older timestamp, but nowhere near a restart.
    EXPECT_FALSE(c.observe(Stream::Audio, f.sender_ts_us - 60'000, f.local_now_us));
    EXPECT_TRUE(c.ready());
}

TEST(MediaClock, ResetIsReusable)
{
    MediaClock c;
    Feeder f { c, Stream::Audio };
    f.transit_us = 10'000;
    f.run(300);
    ASSERT_TRUE(c.ready());

    c.reset();
    EXPECT_FALSE(c.ready());
    EXPECT_EQ(c.offset_us(), 0);

    Feeder g { c, Stream::Audio };
    g.transit_us = 25'000;
    g.run(300);
    EXPECT_TRUE(c.ready());
    EXPECT_EQ(c.offset_us(), 25'000);
}

// Audio and video arrive on separate network threads while the audio callback
// reads the published values. Nothing here may tear or deadlock.
TEST(MediaClock, ConcurrentProducersAndReader)
{
    MediaClock c;
    c.set_video_active(true);
    std::atomic<bool> stop { false };

    auto producer = [&](Stream stream, int64_t transit, int64_t period) {
        Feeder f { c, stream };
        f.transit_us = transit;
        f.period_us = period;
        while (!stop.load(std::memory_order_relaxed)) {
            f.run(20, 20'000);
        }
    };

    std::thread a(producer, Stream::Audio, 10'000, kPacketUs);
    std::thread v(producer, Stream::Video, 70'000, kFrameUs);

    int64_t reads = 0;
    for (int i = 0; i < 200'000; ++i) {
        const int64_t d = c.deadline_us(1'000'000);
        const int64_t t = c.target_delay_us();
        EXPECT_GE(t, 0);
        EXPECT_LE(t, sync_defaults::kMaxDelayUs);
        reads += d;
    }
    stop.store(true, std::memory_order_relaxed);
    a.join();
    v.join();

    EXPECT_NE(reads, 0);
    EXPECT_TRUE(c.ready());
}
