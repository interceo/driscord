#include <gtest/gtest.h>

#include "log.hpp"
#include "utils/protocol.hpp"
#include "video/video.hpp"
#include "video/video_codec.hpp"

#include <cstring>
#include <thread>
#include <vector>

struct SuppressVideoRecvLogs {
    SuppressVideoRecvLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
static SuppressVideoRecvLogs suppress_;

static std::vector<uint8_t> make_bgra(int w, int h)
{
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        frame[i * 4 + 0] = 32;
        frame[i * 4 + 1] = 64;
        frame[i * 4 + 2] = 128;
        frame[i * 4 + 3] = 255;
    }
    return frame;
}

// Build a wire-format packet: VideoHeader + encoded H.264 payload.
static std::vector<uint8_t> make_video_packet(
    const std::vector<uint8_t>& encoded, int w, int h,
    protocol::VideoCodec codec = protocol::VideoCodec::H264)
{
    protocol::VideoHeader vh {
        .width = static_cast<uint32_t>(w),
        .height = static_cast<uint32_t>(h),
        .sender_ts = utils::WallNow(),
        .bitrate_kbps = 500,
        .frame_duration_us = 33333,
        .codec = codec,
    };
    std::vector<uint8_t> pkt(protocol::VideoHeader::kWireSize + encoded.size());
    vh.serialize(pkt.data());
    std::memcpy(pkt.data() + protocol::VideoHeader::kWireSize,
        encoded.data(), encoded.size());
    return pkt;
}

// Regression test: VideoReceiver must lazy-init the decoder on the first packet.
// The bug was: decoder_.ready() was checked *before* the lazy init block,
// so every packet was silently dropped and the decoder was never initialised.
TEST(VideoReceiver, LazyDecoderInitOnFirstPacket)
{
    constexpr int W = 320, H = 240;

    // Encode a real keyframe.
    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 500).has_value());
    enc.force_keyframe();

    auto bgra = make_bgra(W, H);
    std::vector<uint8_t> encoded;
    for (int i = 0; i < 30; ++i) {
        const auto& out = enc.encode(bgra, W, H);
        if (!out.empty()) {
            encoded = out;
            break;
        }
    }
    ASSERT_FALSE(encoded.empty()) << "encoder produced no output";

    // Push the encoded frame into a fresh VideoReceiver (decoder not yet inited).
    VideoReceiver recv("test-peer", 10);
    auto pkt = make_video_packet(encoded, W, H, enc.codec());

    recv.push_video_packet(
        utils::vector_view<const uint8_t>(pkt.data(), pkt.size()), 1);

    auto stats = recv.video_stats();
    EXPECT_GT(stats.packets_received, 0u)
        << "VideoReceiver dropped the first packet — decoder was never lazy-initialised";
}

// After lazy init, the receiver should eventually produce a decoded frame.
TEST(VideoReceiver, DecodesFrameAfterLazyInit)
{
    constexpr int W = 320, H = 240;

    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 500).has_value());
    enc.force_keyframe();

    auto bgra = make_bgra(W, H);

    VideoReceiver recv("test-peer", 1);
    bool got_frame = false;

    // Feed several frames — HW decoders may need a priming pass.
    for (uint64_t fid = 1; fid <= 30 && !got_frame; ++fid) {
        const auto& out = enc.encode(bgra, W, H);
        if (out.empty()) continue;

        auto pkt = make_video_packet(out, W, H, enc.codec());
        recv.push_video_packet(
            utils::vector_view<const uint8_t>(pkt.data(), pkt.size()), fid);
    }

    // Let the jitter buffer prime (buffer_ms=1).
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    recv.update([&](const VideoReceiver::Frame& f) {
        EXPECT_EQ(f.width, W);
        EXPECT_EQ(f.height, H);
        EXPECT_FALSE(f.rgba.empty());
        got_frame = true;
    });

    EXPECT_TRUE(got_frame)
        << "VideoReceiver never produced a decoded frame";
}
