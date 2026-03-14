#include "app.hpp"

#include <algorithm>
#include <cstring>

#include "log.hpp"
#include "utils/byte_utils.hpp"

namespace {

using namespace drist;

constexpr size_t kVideoHeaderSize = 16;  // width(4) + height(4) + timestamp(4) + bitrate_kbps(4)
constexpr size_t kChunkHeaderSize = 6;   // frame_id(2) + chunk_idx(2) + total_chunks(2)
constexpr size_t kMaxChunkPayload = 60000;
constexpr int kStaleVideoSeconds = 3;

}  // namespace

App::App(const Config& cfg) : config_(cfg) {
    for (auto& ts : cfg.turn_servers) {
        transport_.add_turn_server(ts.url, ts.user, ts.pass);
    }

    transport_.on_audio_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        audio_.feed_packet(data, len, peer_volume(peer_id));
    });

    transport_.on_video_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        on_video_packet(peer_id, data, len);
    });

    transport_.on_screen_audio_received([this](const std::string& /*peer_id*/, const uint8_t* data, size_t len) {
        audio_.feed_screen_audio_packet(data, len);
    });

    transport_.on_peer_joined([](const std::string& peer_id) { LOG_INFO() << "peer joined: " << peer_id; });

    transport_.on_video_channel_opened([this]() {
        if (sharing_) {
            video_encoder_.force_keyframe();
        }
    });

    transport_.on_keyframe_requested([this]() {
        if (sharing_) {
            LOG_INFO() << "keyframe requested by peer";
            video_encoder_.force_keyframe();
        }
    });

    transport_.on_peer_left([this](const std::string& peer_id) {
        LOG_INFO() << "peer left: " << peer_id;
        {
            std::scoped_lock lk(video_mutex_);
            peer_video_.erase(peer_id);
            pending_removals_.push_back(peer_id);
        }
        {
            std::scoped_lock lk(peer_vol_mutex_);
            peer_volumes_.erase(peer_id);
        }
    });
}

App::~App() { disconnect(); }

void App::update() {
    if (state_ == AppState::Connecting && transport_.connected()) {
        state_ = AppState::Connected;
        LOG_INFO() << "connected, id: " << transport_.local_id();

        bool ok = audio_.start([this](const uint8_t* data, size_t len) { transport_.send_audio(data, len); });

        if (!ok) {
            LOG_ERROR() << "failed to start audio engine";
        }
    }

    struct PendingDecode {
        std::string peer_id;
        std::shared_ptr<PeerVideoState> vs;
        std::vector<uint8_t> data;
        uint32_t kbps;
        uint32_t sender_ts;
    };
    std::vector<PendingDecode> pending_decodes;
    std::vector<std::string> to_remove;

    {
        std::scoped_lock lk(video_mutex_);
        auto now = std::chrono::steady_clock::now();

        to_remove.swap(pending_removals_);

        for (auto& [peer_id, vs] : peer_video_) {
            if (vs->has_pending_decode) {
                PendingDecode pd;
                pd.peer_id = peer_id;
                pd.vs = vs;
                pd.data.swap(vs->pending_decode);
                pd.kbps = vs->pending_kbps;
                pd.sender_ts = vs->pending_sender_ts;
                vs->has_pending_decode = false;
                pending_decodes.push_back(std::move(pd));
            }
        }

        for (auto it = peer_video_.begin(); it != peer_video_.end();) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second->last_frame);
            if (age.count() > kStaleVideoSeconds) {
                to_remove.push_back(it->first);
                it = peer_video_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& pd : pending_decodes) {
        std::vector<uint8_t> rgba;
        int dec_w = 0, dec_h = 0;
        if (pd.vs->decoder.decode(pd.data.data(), pd.data.size(), rgba, dec_w, dec_h)) {
            pd.vs->decode_failures = 0;
            pd.vs->measured_kbps = static_cast<int>(pd.kbps);

            TimedFrame tf;
            tf.rgba = std::move(rgba);
            tf.width = dec_w;
            tf.height = dec_h;
            tf.sender_ts = pd.sender_ts;
            pd.vs->frame_queue.push_back(std::move(tf));

            while (pd.vs->frame_queue.size() > kMaxFrameQueue) {
                pd.vs->frame_queue.pop_front();
            }
        } else {
            ++pd.vs->decode_failures;
            if (pd.vs->decode_failures % 5 == 1) {
                transport_.send_keyframe_request();
            }
        }
    }

    const uint32_t audio_ts = audio_.screen_playback_ts();
    const bool has_audio_clock = (audio_ts > 0);

    {
        std::scoped_lock lk(video_mutex_);
        for (auto& [peer_id, vs] : peer_video_) {
            if (vs->frame_queue.empty()) {
                continue;
            }

            if (!has_audio_clock) {
                auto& f = vs->frame_queue.back();
                video_renderer_.update_frame(peer_id, f.rgba.data(), f.width, f.height);
                vs->width = f.width;
                vs->height = f.height;
                vs->frame_queue.clear();
                continue;
            }

            int last_ready = -1;
            for (int i = 0; i < static_cast<int>(vs->frame_queue.size()); ++i) {
                if (vs->frame_queue[i].sender_ts <= audio_ts) {
                    last_ready = i;
                } else {
                    break;
                }
            }
            if (last_ready >= 0) {
                auto& f = vs->frame_queue[last_ready];
                video_renderer_.update_frame(peer_id, f.rgba.data(), f.width, f.height);
                vs->width = f.width;
                vs->height = f.height;
                vs->frame_queue.erase(vs->frame_queue.begin(), vs->frame_queue.begin() + last_ready + 1);
            }
        }
    }

    for (auto& pid : to_remove) {
        video_renderer_.remove_peer(pid);
    }
}

void App::connect(const std::string& server_url) {
    if (state_ != AppState::Disconnected) {
        return;
    }

    state_ = AppState::Connecting;
    LOG_INFO() << "connecting to " << server_url << "...";
    transport_.connect(server_url);
}

void App::disconnect() {
    stop_sharing();
    audio_.stop();
    transport_.disconnect();
    state_ = AppState::Disconnected;
    {
        std::scoped_lock lk(video_mutex_);
        peer_video_.clear();
        pending_removals_.clear();
    }
    video_renderer_.cleanup();
}

void App::toggle_mute() { audio_.set_muted(!audio_.muted()); }

void App::toggle_deafen() {
    bool new_deaf = !audio_.deafened();
    audio_.set_deafened(new_deaf);
}

void App::set_volume(float vol) { audio_.set_output_volume(vol); }

void App::set_peer_volume(const std::string& peer_id, float vol) {
    std::scoped_lock lk(peer_vol_mutex_);
    peer_volumes_[peer_id] = vol;
}

float App::peer_volume(const std::string& peer_id) const {
    std::scoped_lock lk(peer_vol_mutex_);
    auto it = peer_volumes_.find(peer_id);
    return (it != peer_volumes_.end()) ? it->second : 1.0f;
}

void App::start_sharing(const CaptureTarget& target, StreamQuality quality, int fps, bool share_audio) {
    if (sharing_ || state_ != AppState::Connected) {
        return;
    }

    int max_w, max_h;
    if (quality == StreamQuality::Source) {
        max_w = 7680;
        max_h = 4320;
    } else {
        auto idx = static_cast<int>(quality);
        max_w = kStreamPresets[idx].width;
        max_h = kStreamPresets[idx].height;
    }

    int enc_w = target.width & ~1;
    int enc_h = target.height & ~1;
    if (enc_w > max_w || enc_h > max_h) {
        float scale = std::min(static_cast<float>(max_w) / enc_w, static_cast<float>(max_h) / enc_h);
        enc_w = static_cast<int>(enc_w * scale) & ~1;
        enc_h = static_cast<int>(enc_h * scale) & ~1;
    }

    if (enc_w <= 0 || enc_h <= 0) {
        LOG_ERROR() << "invalid capture dimensions";
        return;
    }

    int base_br = config_.video_bitrate_kbps;
    if (!video_encoder_.init(enc_w, enc_h, fps, base_br)) {
        LOG_ERROR() << "failed to init video encoder";
        return;
    }

    encoding_ = false;
    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(fps, target, max_w, max_h, [this, fps, base_br](const ScreenCapture::Frame& frame) {
            if (encoding_.exchange(true)) {
                return;
            }

            if (frame.width != video_encoder_.width() || frame.height != video_encoder_.height()) {
                if (!video_encoder_.reinit(frame.width, frame.height, fps, base_br)) {
                    encoding_ = false;
                    return;
                }
                LOG_INFO() << "reinit video encoder: " << frame.width << "x" << frame.height;
            }

            const auto& encoded = video_encoder_.encode(frame.data.data(), frame.width, frame.height);
            if (encoded.empty()) {
                encoding_ = false;
                return;
            }

            size_t full_size = kVideoHeaderSize + encoded.size();
            frame_buf_.resize(full_size);
            write_u32_le(frame_buf_.data() + 0, static_cast<uint32_t>(frame.width));
            write_u32_le(frame_buf_.data() + 4, static_cast<uint32_t>(frame.height));
            write_u32_le(frame_buf_.data() + 8, now_ms());
            write_u32_le(frame_buf_.data() + 12, static_cast<uint32_t>(video_encoder_.measured_kbps()));
            std::memcpy(frame_buf_.data() + kVideoHeaderSize, encoded.data(), encoded.size());

            uint16_t fid = send_frame_id_++;
            size_t total_chunks = (frame_buf_.size() + kMaxChunkPayload - 1) / kMaxChunkPayload;
            if (total_chunks > 0xFFFF) {
                encoding_ = false;
                return;
            }

            for (size_t ci = 0; ci < total_chunks; ++ci) {
                size_t offset = ci * kMaxChunkPayload;
                size_t chunk_len = std::min(kMaxChunkPayload, frame_buf_.size() - offset);

                send_buf_.resize(kChunkHeaderSize + chunk_len);
                write_u16_le(send_buf_.data() + 0, fid);
                write_u16_le(send_buf_.data() + 2, static_cast<uint16_t>(ci));
                write_u16_le(send_buf_.data() + 4, static_cast<uint16_t>(total_chunks));
                std::memcpy(send_buf_.data() + kChunkHeaderSize, frame_buf_.data() + offset, chunk_len);

                transport_.send_video(send_buf_.data(), send_buf_.size());
            }

            encoding_ = false;
        }))
    {
        LOG_ERROR() << "failed to start screen capture";
        video_encoder_.shutdown();
        return;
    }

    clear_preview();
    sharing_ = true;

    if (share_audio && SystemAudioCapture::available()) {
        if (audio_.init_screen_audio([this](const uint8_t* data, size_t len) {
                transport_.send_screen_audio(data, len);
            }))
        {
            system_audio_capture_ = SystemAudioCapture::create();
            if (system_audio_capture_ &&
                system_audio_capture_->start([this](const float* samples, size_t frames, int ch) {
                    audio_.feed_screen_audio_pcm(samples, frames, ch);
                }))
            {
                sharing_audio_ = true;
                LOG_INFO() << "system audio capture started";
            } else {
                audio_.shutdown_screen_audio();
                system_audio_capture_.reset();
                LOG_ERROR() << "failed to start system audio capture";
            }
        }
    }

    LOG_INFO() << "screen sharing started: " << target.name << " " << enc_w << "x" << enc_h << " @ " << fps << " fps";
}

void App::stop_sharing() {
    if (!sharing_) {
        return;
    }

    if (system_audio_capture_) {
        system_audio_capture_->stop();
        system_audio_capture_.reset();
    }
    audio_.shutdown_screen_audio();
    sharing_audio_ = false;

    if (screen_capture_) {
        screen_capture_->stop();
        screen_capture_.reset();
    }
    video_encoder_.shutdown();
    sharing_ = false;
    LOG_INFO() << "screen sharing stopped";
}

void App::update_preview(const CaptureTarget& target) {
    constexpr int kPreviewW = 384;
    constexpr int kPreviewH = 216;

    auto frame = ScreenCapture::grab_thumbnail(target, kPreviewW, kPreviewH);
    if (frame.data.empty()) {
        return;
    }

    // BGRA -> RGBA channel swap
    for (size_t i = 0; i < frame.data.size(); i += 4) {
        std::swap(frame.data[i], frame.data[i + 2]);
    }

    video_renderer_.update_frame(kPreviewPeerId, frame.data.data(), frame.width, frame.height);
}

void App::clear_preview() { video_renderer_.remove_peer(kPreviewPeerId); }

std::vector<App::PeerView> App::peers() const {
    std::vector<PeerView> result;
    auto ps = transport_.peers();
    result.reserve(ps.size());
    for (auto& p : ps) {
        result.emplace_back(p.id, p.dc_open);
    }
    return result;
}

void App::on_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
    if (len <= kChunkHeaderSize) {
        return;
    }

    uint16_t frame_id = read_u16_le(data + 0);
    uint16_t chunk_idx = read_u16_le(data + 2);
    uint16_t total_chunks = read_u16_le(data + 4);

    if (total_chunks == 0 || chunk_idx >= total_chunks) {
        return;
    }

    const uint8_t* chunk_data = data + kChunkHeaderSize;
    size_t chunk_len = len - kChunkHeaderSize;

    std::scoped_lock lk(video_mutex_);

    auto it = peer_video_.find(peer_id);
    if (it == peer_video_.end()) {
        auto vs = std::make_shared<PeerVideoState>();
        if (!vs->decoder.init()) {
            LOG_ERROR() << "failed to init video decoder for " << peer_id;
            return;
        }
        vs->last_frame = std::chrono::steady_clock::now();
        it = peer_video_.emplace(peer_id, std::move(vs)).first;
    }

    auto& vs = *it->second;

    if (frame_id != vs.reassembly_frame_id || total_chunks != vs.reassembly_total) {
        if (vs.reassembly_total > 0 && vs.reassembly_got < vs.reassembly_total) {
            transport_.send_keyframe_request();
        }
        vs.reassembly_frame_id = frame_id;
        vs.reassembly_total = total_chunks;
        vs.reassembly_got = 0;
        vs.reassembly_buf.clear();
    }

    size_t offset = static_cast<size_t>(chunk_idx) * kMaxChunkPayload;
    size_t needed = offset + chunk_len;
    if (vs.reassembly_buf.size() < needed) {
        vs.reassembly_buf.resize(needed);
    }
    std::memcpy(vs.reassembly_buf.data() + offset, chunk_data, chunk_len);
    ++vs.reassembly_got;

    if (vs.reassembly_got < total_chunks) {
        return;
    }

    auto& buf = vs.reassembly_buf;
    if (buf.size() <= kVideoHeaderSize) {
        return;
    }

    uint32_t w = read_u32_le(buf.data() + 0);
    uint32_t h = read_u32_le(buf.data() + 4);
    uint32_t sender_ts = read_u32_le(buf.data() + 8);
    uint32_t sender_kbps = read_u32_le(buf.data() + 12);

    if (w == 0 || h == 0 || w > 7680 || h > 4320) {
        return;
    }

    vs.pending_decode.assign(buf.data() + kVideoHeaderSize, buf.data() + buf.size());
    vs.pending_kbps = sender_kbps;
    vs.pending_sender_ts = sender_ts;
    vs.has_pending_decode = true;
    vs.last_frame = std::chrono::steady_clock::now();
}

StreamStats App::stream_stats(const std::string& peer_id) const {
    StreamStats s;
    std::scoped_lock lk(video_mutex_);
    auto it = peer_video_.find(peer_id);
    if (it != peer_video_.end()) {
        s.width = it->second->width;
        s.height = it->second->height;
        s.measured_kbps = it->second->measured_kbps;
    }
    return s;
}
