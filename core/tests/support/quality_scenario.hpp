#pragma once

#include "audio/audio.hpp"
#include "media_gen.hpp"
#include "net_cond.hpp"
#include "net_model.hpp"
#include "quality_metrics.hpp"
#include "sim_clock.hpp"
#include "sync/media_clock.hpp"
#include "sync/playout_policy.hpp"
#include "video/video.hpp"
#include "video/video_codec.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace test_util {

// One stretch of the call with a fixed set of network conditions.
struct Phase {
    int64_t duration_us = 0;
    NetProfile audio_net { };
    NetProfile video_net { };
};

struct Scenario {
    std::string name;
    std::vector<Phase> phases;
    bool video_enabled = true;
    int width = 320;
    int height = 240;
    int fps = 30;
    uint64_t seed = 1;
};

struct QualityReport {
    std::string scenario;
    uint64_t seed = 0;
    bool video_enabled = false;
    AudioMetrics audio;
    VideoMetrics video;
    SyncMetrics sync;
    AudioReceiver::Stats audio_stats { };
    VideoReceiver::Stats video_stats { };

    nlohmann::json to_json() const
    {
        nlohmann::json j;
        j["scenario"] = scenario;
        j["seed"] = seed;
        j["audio"] = {
            { "segSnrDb", audio.seg_snr_db },
            { "segSnrP05Db", audio.seg_snr_p05_db },
            { "glitches", audio.glitch_count },
            { "silenceRatio", audio.silence_ratio },
            { "expandRate", audio.expand_rate },
            { "accelerateRate", audio.accelerate_rate },
            { "preemptiveRate", audio.preemptive_rate },
            { "fecRate", audio.fec_rate },
            { "underrunRate", audio.underrun_rate },
            { "samplesOut", audio.samples_out },
            { "targetDelayMs", audio_stats.target_delay_ms },
            { "p95DelayMs", audio_stats.p95_delay_ms },
            { "packetsReceived", audio_stats.packets_received },
            { "drops", audio_stats.drop_count },
        };
        if (video_enabled) {
            j["video"] = {
                { "psnrAvgDb", video.psnr_avg_db },
                { "psnrP05Db", video.psnr_p05_db },
                { "ssimAvg", video.ssim_avg },
                { "ssimP05", video.ssim_p05 },
                { "framesCaptured", video.frames_captured },
                { "framesRendered", video.frames_rendered },
                { "droppedRatio", video.dropped_ratio },
                { "freezes", video.freeze_count },
                { "freezeTotalMs", video.freeze_total_ms },
                { "maxFreezeMs", video.max_freeze_ms },
                { "harmonicFps", video.harmonic_fps },
                { "p95InterFrameMs", video.p95_inter_frame_ms },
                { "timeToFirstFrameMs", video.time_to_first_frame_ms },
                { "transportP50Ms", video.transport_p50_ms },
                { "transportP95Ms", video.transport_p95_ms },
                { "lateFrames", video_stats.late_count },
                { "decodeFailures", video_stats.decode_failures },
            };
            j["sync"] = {
                { "medianSkewMs", sync.median_skew_ms },
                { "p05SkewMs", sync.p05_skew_ms },
                { "p95SkewMs", sync.p95_skew_ms },
                { "maxAbsSkewMs", sync.max_abs_skew_ms },
                { "samples", sync.samples },
            };
        }
        return j;
    }
};

using PolicyFactory = std::function<std::unique_ptr<avsync::PlayoutPolicy>()>;

// Drives both receive pipelines through a modelled network on a simulated
// clock. Nothing here sleeps: a minute of media takes whatever the codecs cost
// and no more, and two runs with the same seed produce the same report.
inline QualityReport run_scenario(const Scenario& sc, const PolicyFactory& policy = { })
{
    static const SuppressLogs quiet;

    constexpr int64_t kAudioPeriodUs = 20'000; // one Opus frame
    constexpr int64_t kStepUs = 1'000;
    const int64_t video_period_us = 1'000'000 / sc.fps;

    int64_t total_us = 0;
    for (const Phase& p : sc.phases) {
        total_us += p.duration_us;
    }

    // --- source material, produced once so codec cost stays out of the loop --
    const size_t audio_frames = static_cast<size_t>(total_us / 1'000'000.0 * opus::kSampleRate);
    std::vector<float> reference = speech_like(audio_frames, sc.seed);

    struct Outgoing {
        int64_t send_at_us = 0;
        uint64_t id = 0;
        std::vector<uint8_t> bytes;
    };

    std::vector<Outgoing> audio_out;
    {
        OpusEncode enc;
        enc.init(opus::kSampleRate, 1, 64000, 2048 /* OPUS_APPLICATION_VOIP */);
        // Every frame is sent, including quiet ones. A sender's noise gate
        // would drop silence and flag the next talkspurt, but then a gap at the
        // receiver would mean two different things and concealment statistics
        // would stop meaning loss. Talkspurt handling has its own test.
        uint32_t seq = 0;
        for (size_t i = 0; (i + 1) * opus::kFrameSize <= reference.size(); ++i) {
            const float* pcm = reference.data() + i * opus::kFrameSize;
            const int64_t ts = static_cast<int64_t>(i) * kAudioPeriodUs;
            auto pkt = encode_audio_packet(enc, pcm, seq, ts,
                seq == 0 ? protocol::flags::kTalkspurtStart : 0u);
            if (!pkt.empty()) {
                audio_out.push_back({ ts, seq, std::move(pkt) });
            }
            ++seq;
        }
    }

    std::vector<Outgoing> video_out;
    if (sc.video_enabled) {
        VideoEncoder venc;
        venc.init(sc.width, sc.height, sc.fps, 1500);
        // The encoder may hold a frame back, so the bitstream that comes out of
        // encode() is not necessarily the frame just fed in. Ids are handed out
        // in submission order and consumed in output order, which keeps every
        // packet stamped with the picture it actually carries — without that,
        // PSNR compares a frame against its neighbour.
        std::deque<uint64_t> pending;
        for (int64_t ts = 0; ts < total_us; ts += video_period_us) {
            const uint64_t id = static_cast<uint64_t>(ts / video_period_us);
            pending.push_back(id);
            const auto bgra = bgra_pattern(id, sc.width, sc.height);
            const auto& encoded = venc.encode(bgra, sc.width, sc.height);
            if (encoded.empty()) {
                continue;
            }
            const uint64_t out_id = pending.front();
            pending.pop_front();
            const int64_t out_ts = static_cast<int64_t>(out_id) * video_period_us;
            video_out.push_back({ ts, out_id,
                wrap_video_packet(encoded, sc.width, sc.height, out_ts, venc.codec(),
                    static_cast<uint32_t>(video_period_us)) });
        }
    }

    // --- receive side --------------------------------------------------------
    SimClock sim;
    auto clock = policy ? std::make_shared<avsync::MediaClock>(policy())
                        : std::make_shared<avsync::MediaClock>();
    clock->set_video_active(sc.video_enabled);

    AudioReceiver audio(clock, 1, opus::kSampleRate, sim);
    VideoReceiver video("peer", clock, sim);

    NetworkModel audio_net(sc.phases.front().audio_net, sc.seed);
    NetworkModel video_net(sc.phases.front().video_net, sc.seed ^ 0x9e37);

    AudioQualityAnalyzer audio_q(reference, opus::kSampleRate);
    SyncAnalyzer sync_q;

    std::vector<uint8_t> ref_cache;
    uint64_t ref_cache_id = UINT64_MAX;
    auto reference_frame = [&](uint64_t id) -> const std::vector<uint8_t>& {
        if (id != ref_cache_id) {
            ref_cache = bgra_pattern(id, sc.width, sc.height);
            ref_cache_id = id;
        }
        return ref_cache;
    };
    VideoQualityAnalyzer video_q(sc.width, sc.height, sc.fps, reference_frame);

    // --- the loop ------------------------------------------------------------
    std::vector<float> out(opus::kFrameSize);
    size_t next_audio = 0, next_video = 0, phase = 0;
    int64_t phase_ends_us = sc.phases.front().duration_us;
    int64_t consumed_frames = 0;
    int64_t next_render_us = 0;
    int64_t last_video_ts_us = -1;

    // Let the tail drain: the last packets still have a playout delay to serve.
    const int64_t end_us = total_us + 1'000'000;

    while (sim.now_us() < end_us) {
        const int64_t now = sim.now_us();

        if (phase + 1 < sc.phases.size() && now >= phase_ends_us) {
            ++phase;
            phase_ends_us += sc.phases[phase].duration_us;
            audio_net.set_profile(sc.phases[phase].audio_net);
            video_net.set_profile(sc.phases[phase].video_net);
        }

        while (next_audio < audio_out.size() && audio_out[next_audio].send_at_us <= now) {
            const auto& p = audio_out[next_audio++];
            audio_net.send(now, p.bytes.data(), p.bytes.size(), p.id);
        }
        while (next_video < video_out.size() && video_out[next_video].send_at_us <= now) {
            const auto& p = video_out[next_video++];
            video_q.on_captured(p.id, p.send_at_us);
            video_net.send(now, p.bytes.data(), p.bytes.size(), p.id);
        }

        audio_net.deliver_due(now, [&](const uint8_t* d, size_t n, uint64_t) {
            audio.push_packet(std::span<const uint8_t>(d, n));
        });
        video_net.deliver_due(now, [&](const uint8_t* d, size_t n, uint64_t id) {
            video.push_video_packet(std::span<const uint8_t>(d, n), id);
        });

        // Stand-in for the sound card: consumes at exactly the device rate.
        const int64_t frames_due = now * opus::kSampleRate / 1'000'000;
        while (consumed_frames + opus::kFrameSize <= frames_due) {
            audio.read(out.data(), out.size());
            audio_q.on_played(out.data(), out.size());
            consumed_frames += opus::kFrameSize;
        }

        if (sc.video_enabled && now >= next_render_us) {
            next_render_us += video_period_us;
            video.update([&](const VideoReceiver::Frame& f) {
                if (f.sender_ts_us == last_video_ts_us) {
                    return; // the renderer holds the last picture between frames
                }
                last_video_ts_us = f.sender_ts_us;
                video_q.on_rendered(
                    static_cast<uint64_t>(f.sender_ts_us / video_period_us),
                    f.rgba.data(), now);

                // Skew is only meaningful at the instant a new picture appears.
                // Sampling it while a frame is held would measure the freeze
                // that is already counted as a freeze. Start-up is excluded too:
                // the shared delay is still growing to cover video there.
                const int64_t playout = audio.stats().playout_ts_us;
                if (clock->ready() && playout > 2'000'000
                    && playout < total_us - 300'000) {
                    sync_q.sample(last_video_ts_us, playout);
                }
            });
        }

        sim.advance(kStepUs);
    }

    QualityReport r;
    r.scenario = sc.name;
    r.seed = sc.seed;
    r.video_enabled = sc.video_enabled;
    r.audio_stats = audio.stats();
    r.video_stats = video.video_stats();
    r.audio = audio_q.finish(r.audio_stats);
    if (sc.video_enabled) {
        r.video = video_q.finish(0);
        r.sync = sync_q.finish();
    }
    return r;
}

} // namespace test_util
