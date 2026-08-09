#pragma once

#include "audio/opus_codec.hpp"
#include "utils/log.hpp"
#include "utils/protocol.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

// Synthetic media shared by tests and benchmarks. Every generator is a pure
// function of its arguments, so a scenario can regenerate the reference frame
// for any frame index instead of keeping a session's worth of raw video.

namespace test_util {

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};

// --- audio ---------------------------------------------------------------

inline std::vector<float> sine(double hz,
    size_t frames,
    int channels = 1,
    double amp = 0.5,
    int sample_rate = opus::kSampleRate)
{
    std::vector<float> v(frames * static_cast<size_t>(channels));
    for (size_t i = 0; i < frames; ++i) {
        const double s = amp
            * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i)
                / sample_rate);
        for (int c = 0; c < channels; ++c) {
            v[i * static_cast<size_t>(channels) + static_cast<size_t>(c)]
                = static_cast<float>(s);
        }
    }
    return v;
}

// Voiced harmonics under a syllable envelope, with silent gaps between
// talkspurts. The gaps are the point: they are what exercises the sender's
// kTalkspurtStart flag and the receiver's refusal to conceal across a pause.
inline std::vector<float> speech_like(size_t frames,
    uint64_t seed = 1,
    int sample_rate = opus::kSampleRate)
{
    std::vector<float> v(frames);
    uint64_t rng = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    const auto next = [&rng] {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((rng >> 33) & 0xFFFFFF) / double(0xFFFFFF);
    };

    const double sr = sample_rate;
    size_t i = 0;
    bool speaking = true;
    while (i < frames) {
        // 0.4-1.2 s of speech, then 0.2-0.6 s of silence.
        const double span_s = speaking ? 0.4 + 0.8 * next() : 0.2 + 0.4 * next();
        const size_t span = static_cast<size_t>(span_s * sr);
        const double f0 = 90.0 + 80.0 * next(); // pitch for this talkspurt

        for (size_t k = 0; k < span && i < frames; ++k, ++i) {
            if (!speaking) {
                v[i] = 0.0f;
                continue;
            }
            const double t = static_cast<double>(k) / sr;
            // Syllable rate around 4 Hz, never fully closing.
            const double env = 0.25 + 0.75 * std::pow(
                                   0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * 4.0 * t)), 0.6);
            double s = 0.0;
            for (int h = 1; h <= 5; ++h) {
                s += std::sin(2.0 * std::numbers::pi * f0 * h * t) / h;
            }
            v[i] = static_cast<float>(0.35 * env * s);
        }
        speaking = !speaking;
    }
    return v;
}

// --- video ---------------------------------------------------------------

// A moving pattern with real spatial detail. Flat colour would make PSNR
// infinite and SSIM meaningless, and a static image would hide every freeze.
inline std::vector<uint8_t> bgra_pattern(uint64_t frame_idx, int w, int h)
{
    std::vector<uint8_t> f(static_cast<size_t>(w) * h * 4);
    const int shift = static_cast<int>(frame_idx * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int u = x + shift;
            const int v = y - shift;
            // Diagonal gradient, a moving checker, and a fine dither so the
            // encoder cannot represent the frame for free.
            const uint8_t base = static_cast<uint8_t>((u * 3 + v * 5) & 0xFF);
            const uint8_t check = ((u >> 4) + (v >> 4)) & 1 ? 60 : 0;
            const uint8_t dither = static_cast<uint8_t>(((x * 7 + y * 13) & 0x1F));
            uint8_t* p = f.data() + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = static_cast<uint8_t>(base + check);          // B
            p[1] = static_cast<uint8_t>(base / 2 + dither);     // G
            p[2] = static_cast<uint8_t>(255 - base + check / 2); // R
            p[3] = 255;
        }
    }
    return f;
}

// --- wire framing --------------------------------------------------------

inline std::vector<uint8_t> encode_audio_packet(OpusEncode& enc,
    const float* pcm,
    uint32_t seq,
    int64_t sender_ts_us,
    uint32_t flags = 0)
{
    std::vector<uint8_t> opus_buf(opus::kMaxPacket);
    const int len = enc.encode(pcm, opus::kFrameSize, opus_buf.data(), opus::kMaxPacket);
    if (len <= 0) {
        return { };
    }

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

inline std::vector<uint8_t> wrap_video_packet(const std::vector<uint8_t>& encoded,
    int w,
    int h,
    int64_t sender_ts_us,
    protocol::VideoCodec codec,
    uint32_t frame_duration_us,
    uint32_t bitrate_kbps = 500)
{
    const protocol::VideoHeader vh {
        .width = static_cast<uint32_t>(w),
        .height = static_cast<uint32_t>(h),
        .sender_ts_us = sender_ts_us,
        .bitrate_kbps = bitrate_kbps,
        .frame_duration_us = frame_duration_us,
        .flags = 0,
        .codec = codec,
    };
    std::vector<uint8_t> pkt(protocol::VideoHeader::kWireSize + encoded.size());
    vh.serialize(pkt.data());
    std::memcpy(pkt.data() + protocol::VideoHeader::kWireSize, encoded.data(),
        encoded.size());
    return pkt;
}

} // namespace test_util
