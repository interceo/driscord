#include <gtest/gtest.h>

#include "log.hpp"
#include "utils/protocol.hpp"
#include "video/video_codec.hpp"

#include <cstring>
#include <vector>

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
static SuppressLogs suppress_logs_on_startup;

// Synthetic BGRA frame filled with a solid colour.
static std::vector<uint8_t> make_bgra(int w, int h,
    uint8_t b = 32, uint8_t g = 64, uint8_t r = 128)
{
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        frame[i * 4 + 0] = b;
        frame[i * 4 + 1] = g;
        frame[i * 4 + 2] = r;
        frame[i * 4 + 3] = 255;
    }
    return frame;
}

// Encode one keyframe, retrying up to `tries` times. Returns empty on failure.
static std::vector<uint8_t> encode_one_frame(VideoEncoder& enc,
    int w, int h, int tries = 10)
{
    auto frame = make_bgra(w, h);
    enc.force_keyframe();
    for (int i = 0; i < tries; ++i) {
        const auto& out = enc.encode(frame, w, h);
        if (!out.empty()) {
            return out;
        }
    }
    return { };
}

// ---- VideoEncoder ----------------------------------------------------------

TEST(VideoEncoder, InitValidParams)
{
    VideoEncoder enc;
    auto res = enc.init(320, 240, 30, 500);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(enc.width(), 320);
    EXPECT_EQ(enc.height(), 240);
    EXPECT_EQ(enc.fps(), 30);
}

TEST(VideoEncoder, InitOddWidth)
{
    VideoEncoder enc;
    auto res = enc.init(321, 240, 30, 500);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), VideoError::InvalidDimensions);
}

TEST(VideoEncoder, InitOddHeight)
{
    VideoEncoder enc;
    auto res = enc.init(320, 241, 30, 500);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), VideoError::InvalidDimensions);
}

TEST(VideoEncoder, InitZeroDimensions)
{
    VideoEncoder enc;
    auto res = enc.init(0, 0, 30, 500);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), VideoError::InvalidDimensions);
}

TEST(VideoEncoder, InitZeroFps)
{
    VideoEncoder enc;
    auto res = enc.init(320, 240, 0, 500);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), VideoError::InvalidFps);
}

TEST(VideoEncoder, InitZeroBitrate)
{
    VideoEncoder enc;
    auto res = enc.init(320, 240, 30, 0);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), VideoError::InvalidBitrate);
}

TEST(VideoEncoder, InitSameParamsIsNoop)
{
    VideoEncoder enc;
    ASSERT_TRUE(enc.init(320, 240, 30, 500).has_value());
    // Second call with identical params should succeed without reinitialisation.
    EXPECT_TRUE(enc.init(320, 240, 30, 500).has_value());
    EXPECT_EQ(enc.width(), 320);
}

TEST(VideoEncoder, ReinitWithNewParams)
{
    VideoEncoder enc;
    ASSERT_TRUE(enc.init(320, 240, 30, 500).has_value());
    ASSERT_TRUE(enc.init(640, 480, 30, 1000).has_value());
    EXPECT_EQ(enc.width(), 640);
    EXPECT_EQ(enc.height(), 480);
}

TEST(VideoEncoder, EncodeProducesOutput)
{
    VideoEncoder enc;
    ASSERT_TRUE(enc.init(320, 240, 30, 500).has_value());
    const auto out = encode_one_frame(enc, 320, 240);
    EXPECT_FALSE(out.empty());
}

TEST(VideoEncoder, EncodeDimensionMismatch)
{
    VideoEncoder enc;
    ASSERT_TRUE(enc.init(320, 240, 30, 500).has_value());

    auto frame = make_bgra(640, 480);
    const auto& out = enc.encode(frame, 640, 480);
    EXPECT_TRUE(out.empty());
}

TEST(VideoEncoder, EncodeBeforeInitIsEmpty)
{
    VideoEncoder enc;
    auto frame = make_bgra(320, 240);
    const auto& out = enc.encode(frame, 320, 240);
    EXPECT_TRUE(out.empty());
}

TEST(VideoEncoder, ShutdownMakesEncodeEmpty)
{
    VideoEncoder enc;
    ASSERT_TRUE(enc.init(320, 240, 30, 500).has_value());
    enc.shutdown();

    auto frame = make_bgra(320, 240);
    const auto& out = enc.encode(frame, 320, 240);
    EXPECT_TRUE(out.empty());
}

// ---- VideoDecoder ----------------------------------------------------------

TEST(VideoDecoder, NotReadyBeforeInit)
{
    VideoDecoder dec;
    EXPECT_FALSE(dec.ready());
}

TEST(VideoDecoder, InitH264Succeeds)
{
    VideoDecoder dec;
    EXPECT_TRUE(dec.init(protocol::VideoCodec::H264));
    EXPECT_TRUE(dec.ready());
}

TEST(VideoDecoder, InitHEVCSucceeds)
{
    VideoDecoder dec;
    EXPECT_TRUE(dec.init(protocol::VideoCodec::HEVC));
    EXPECT_TRUE(dec.ready());
}

TEST(VideoDecoder, ShutdownClearsReady)
{
    VideoDecoder dec;
    ASSERT_TRUE(dec.init());
    dec.shutdown();
    EXPECT_FALSE(dec.ready());
}

TEST(VideoDecoder, ReinitSucceeds)
{
    VideoDecoder dec;
    ASSERT_TRUE(dec.init());
    dec.shutdown();
    ASSERT_TRUE(dec.init());
    EXPECT_TRUE(dec.ready());
}

TEST(VideoDecoder, ReinitWithDifferentCodec)
{
    VideoDecoder dec;
    ASSERT_TRUE(dec.init(protocol::VideoCodec::H264));
    dec.shutdown();
    ASSERT_TRUE(dec.init(protocol::VideoCodec::HEVC));
    EXPECT_TRUE(dec.ready());
}

TEST(VideoDecoder, DecodeWithoutInitReturnsFalse)
{
    VideoDecoder dec;
    const uint8_t buf[] = { 0x00, 0x00, 0x00, 0x01 };
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    EXPECT_FALSE(dec.decode(buf, sizeof(buf), rgba, w, h));
}

TEST(VideoDecoder, DecodeGarbageReturnsFalse)
{
    VideoDecoder dec;
    ASSERT_TRUE(dec.init());

    const uint8_t garbage[] = { 0x00, 0x01, 0x02, 0x03, 0xFF, 0xFF, 0xDE, 0xAD };
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    EXPECT_FALSE(dec.decode(garbage, sizeof(garbage), rgba, w, h));
}

// ---- Roundtrip (encoder → decoder) ----------------------------------------

// Helper: keep feeding frames until the decoder produces at least one output.
// Hardware decoders (e.g. h264_cuvid) buffer N frames internally before
// emitting the first output — they need this "priming" pass.
static bool prime_and_decode(VideoEncoder& enc, VideoDecoder& dec,
    int w, int h,
    std::vector<uint8_t>& rgba_out, int& out_w, int& out_h,
    int max_iters = 30)
{
    auto frame = make_bgra(w, h);
    for (int i = 0; i < max_iters; ++i) {
        const auto& encoded = enc.encode(frame, w, h);
        if (!encoded.empty()) {
            if (dec.decode(encoded.data(), encoded.size(), rgba_out, out_w, out_h)) {
                return true;
            }
        }
    }
    return false;
}

TEST(VideoCodecRoundtrip, EncodeDecode)
{
    constexpr int W = 320, H = 240;

    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 500).has_value());

    VideoDecoder dec;
    ASSERT_TRUE(dec.init(enc.codec()));

    enc.force_keyframe();

    std::vector<uint8_t> rgba;
    int out_w = 0, out_h = 0;
    ASSERT_TRUE(prime_and_decode(enc, dec, W, H, rgba, out_w, out_h))
        << "decoder never produced a frame";

    EXPECT_EQ(out_w, W);
    EXPECT_EQ(out_h, H);
    EXPECT_EQ(static_cast<int>(rgba.size()), W * H * 4);
}

TEST(VideoCodecRoundtrip, MultipleFrames)
{
    constexpr int W = 320, H = 240;

    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 500).has_value());

    VideoDecoder dec;
    ASSERT_TRUE(dec.init(enc.codec()));

    auto frame = make_bgra(W, H);
    enc.force_keyframe();

    int decoded_count = 0;
    for (int i = 0; i < 30; ++i) {
        const auto& encoded = enc.encode(frame, W, H);
        if (encoded.empty()) {
            continue;
        }

        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (dec.decode(encoded.data(), encoded.size(), rgba, w, h)) {
            EXPECT_EQ(w, W);
            EXPECT_EQ(h, H);
            EXPECT_EQ(static_cast<int>(rgba.size()), W * H * 4);
            ++decoded_count;
        }
    }
    EXPECT_GT(decoded_count, 0);
}

TEST(VideoCodecRoundtrip, ReinitDecoderMidStream)
{
    constexpr int W = 320, H = 240;

    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 500).has_value());

    VideoDecoder dec;
    ASSERT_TRUE(dec.init(enc.codec()));

    // Prime both pipelines before the reinit.
    enc.force_keyframe();
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    ASSERT_TRUE(prime_and_decode(enc, dec, W, H, rgba, w, h))
        << "pipeline priming failed";

    // Reinit the decoder (simulates a peer reconnect / decoder reset).
    dec.shutdown();
    ASSERT_TRUE(dec.init(enc.codec()));

    // Force a new keyframe so the fresh decoder has a self-contained entry point.
    enc.force_keyframe();

    auto frame2 = make_bgra(W, H);
    bool got_frame = false;
    for (int i = 0; i < 30 && !got_frame; ++i) {
        const auto& encoded = enc.encode(frame2, W, H);
        if (!encoded.empty()) {
            int fw = 0, fh = 0;
            if (dec.decode(encoded.data(), encoded.size(), rgba, fw, fh)) {
                EXPECT_EQ(fw, W);
                EXPECT_EQ(fh, H);
                EXPECT_EQ(static_cast<int>(rgba.size()), W * H * 4);
                got_frame = true;
            }
        }
    }
    EXPECT_TRUE(got_frame);
}

TEST(VideoCodecRoundtrip, ForceKeyframe)
{
    constexpr int W = 320, H = 240;

    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 500).has_value());

    VideoDecoder dec;
    ASSERT_TRUE(dec.init(enc.codec()));

    auto frame = make_bgra(W, H);

    // Prime both pipelines.
    enc.force_keyframe();
    int primed = 0;
    for (int i = 0; i < 30 && primed < 2; ++i) {
        const auto& e = enc.encode(frame, W, H);
        if (!e.empty()) {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if (dec.decode(e.data(), e.size(), rgba, w, h)) {
                ++primed;
            }
        }
    }
    ASSERT_GT(primed, 0) << "pipeline priming failed";

    // Force a new keyframe (repeat_headers=1 ensures SPS+PPS are inline in the
    // IDR so the stream remains decodable after the keyframe).
    enc.force_keyframe();

    bool got_frame = false;
    for (int i = 0; i < 30 && !got_frame; ++i) {
        const auto& e = enc.encode(frame, W, H);
        if (!e.empty()) {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if ((got_frame = dec.decode(e.data(), e.size(), rgba, w, h))) {
                EXPECT_EQ(w, W);
                EXPECT_EQ(h, H);
            }
        }
    }
    EXPECT_TRUE(got_frame) << "decoder stopped producing frames after force_keyframe";
}

// ---- HEVC roundtrip (above 2K threshold) -----------------------------------

TEST(VideoCodecRoundtrip, HEVCEncodeDecode)
{
    // 2560x1440 crosses the 2048-px threshold → encoder should pick HEVC.
    constexpr int W = 2560, H = 1440;

    VideoEncoder enc;
    ASSERT_TRUE(enc.init(W, H, 30, 8000).has_value());
    EXPECT_EQ(enc.codec(), protocol::VideoCodec::HEVC)
        << "expected HEVC encoder for resolution above 2K";

    VideoDecoder dec;
    ASSERT_TRUE(dec.init(enc.codec()));

    enc.force_keyframe();

    std::vector<uint8_t> rgba;
    int out_w = 0, out_h = 0;
    ASSERT_TRUE(prime_and_decode(enc, dec, W, H, rgba, out_w, out_h))
        << "HEVC decoder never produced a frame";

    EXPECT_EQ(out_w, W);
    EXPECT_EQ(out_h, H);
    EXPECT_EQ(static_cast<int>(rgba.size()), W * H * 4);
}
