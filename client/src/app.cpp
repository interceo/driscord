#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "log.hpp"

namespace {

constexpr size_t kVideoHeaderSize = 16;  // width(4) + height(4) + timestamp(4) + bitrate_kbps(4)
constexpr int kStaleVideoSeconds = 3;

uint32_t now_ms() {
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    return static_cast<uint32_t>(ms);
}

void write_u32_le(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t read_u32_le(const uint8_t* src) {
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

}  // namespace

App::App(const Config& cfg) : config_(cfg) {
    transport_.on_audio_received([this](const std::string& /*peer_id*/, const uint8_t* data, size_t len) {
        audio_.feed_packet(data, len);
    });

    transport_.on_video_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        on_video_packet(peer_id, data, len);
    });

    transport_.on_peer_joined([](const std::string& peer_id) { LOG_INFO() << "peer joined: " << peer_id; });

    transport_.on_peer_left([this](const std::string& peer_id) {
        LOG_INFO() << "peer left: " << peer_id;
        std::scoped_lock lk(video_mutex_);
        peer_video_.erase(peer_id);
        pending_removals_.push_back(peer_id);
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

    std::vector<std::string> to_remove;
    {
        std::scoped_lock lk(video_mutex_);
        auto now = std::chrono::steady_clock::now();

        to_remove.swap(pending_removals_);

        for (auto& [peer_id, vs] : peer_video_) {
            if (vs->dirty) {
                video_renderer_.update_frame(peer_id, vs->rgba.data(), vs->width, vs->height);
                vs->dirty = false;
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

void App::start_sharing(const CaptureTarget& target, int preset_idx, int fps) {
    if (sharing_ || state_ != AppState::Connected) {
        return;
    }

    int max_w, max_h;
    if (preset_idx <= 0 || preset_idx >= kStreamPresetCount) {
        max_w = 7680;
        max_h = 4320;
    } else {
        max_w = kStreamPresets[preset_idx].width;
        max_h = kStreamPresets[preset_idx].height;
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

    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(fps, target, max_w, max_h, [this, fps, base_br](const ScreenCapture::Frame& frame) {
            if (frame.width != video_encoder_.width() || frame.height != video_encoder_.height()) {
                if (!video_encoder_.reinit(frame.width, frame.height, fps, base_br)) {
                    return;
                }
                LOG_INFO() << "reinit video encoder: " << frame.width << "x" << frame.height;
            }

            auto encoded = video_encoder_.encode(frame.data.data(), frame.width, frame.height);
            if (encoded.empty()) {
                return;
            }

            std::vector<uint8_t> packet(kVideoHeaderSize + encoded.size());
            write_u32_le(packet.data() + 0, static_cast<uint32_t>(frame.width));
            write_u32_le(packet.data() + 4, static_cast<uint32_t>(frame.height));
            write_u32_le(packet.data() + 8, now_ms());
            write_u32_le(packet.data() + 12, static_cast<uint32_t>(video_encoder_.measured_kbps()));
            std::memcpy(packet.data() + kVideoHeaderSize, encoded.data(), encoded.size());

            transport_.send_video(packet.data(), packet.size());
        }))
    {
        LOG_ERROR() << "failed to start screen capture";
        video_encoder_.shutdown();
        return;
    }

    clear_preview();
    sharing_ = true;
    LOG_INFO() << "screen sharing started: " << target.name << " " << enc_w << "x" << enc_h << " @ " << fps << " fps";
}

void App::stop_sharing() {
    if (!sharing_) {
        return;
    }

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
    if (len <= kVideoHeaderSize) {
        return;
    }

    uint32_t w = read_u32_le(data + 0);
    uint32_t h = read_u32_le(data + 4);
    uint32_t sender_kbps = read_u32_le(data + 12);

    const uint8_t* payload = data + kVideoHeaderSize;
    size_t payload_len = len - kVideoHeaderSize;

    if (w == 0 || h == 0 || w > 7680 || h > 4320) {
        return;
    }

    std::scoped_lock lk(video_mutex_);

    auto it = peer_video_.find(peer_id);
    if (it == peer_video_.end()) {
        auto vs = std::make_unique<PeerVideoState>();
        if (!vs->decoder.init()) {
            LOG_ERROR() << "failed to init video decoder for " << peer_id;
            return;
        }
        vs->last_frame = std::chrono::steady_clock::now();
        it = peer_video_.emplace(peer_id, std::move(vs)).first;
    }

    auto& vs = *it->second;
    vs.measured_kbps = static_cast<int>(sender_kbps);

    auto now = std::chrono::steady_clock::now();
    if (vs.decoder.decode(payload, payload_len, vs.rgba, vs.width, vs.height)) {
        vs.dirty = true;
        vs.last_frame = now;
    }
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
