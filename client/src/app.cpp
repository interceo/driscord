#include "app.hpp"

#include <chrono>
#include <cstring>

#include "log.hpp"

namespace {

constexpr size_t kVideoHeaderSize = 12;  // width(4) + height(4) + timestamp(4)

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
        {
            std::scoped_lock lk(video_mutex_);
            peer_video_.erase(peer_id);
        }
        video_renderer_.remove_peer(peer_id);
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

    // Drain pending decoded video frames → upload to GL textures (main thread).
    {
        std::scoped_lock lk(video_mutex_);
        for (auto& [peer_id, vs] : peer_video_) {
            if (vs->dirty) {
                video_renderer_.update_frame(peer_id, vs->rgba.data(), vs->width, vs->height);
                vs->dirty = false;
            }
        }
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
    }
    video_renderer_.cleanup();
}

void App::toggle_mute() { audio_.set_muted(!audio_.muted()); }

void App::set_volume(float vol) { audio_.set_output_volume(vol); }

void App::start_sharing() {
    if (sharing_ || state_ != AppState::Connected) {
        return;
    }

    int cw = config_.capture_width;
    int ch = config_.capture_height;

    if (!video_encoder_.init(cw, ch, config_.video_bitrate_kbps)) {
        LOG_ERROR() << "failed to init video encoder";
        return;
    }

    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(config_.screen_fps, cw, ch, [this](const ScreenCapture::Frame& frame) {
            auto vp8 = video_encoder_.encode(frame.data.data(), frame.width, frame.height);
            if (vp8.empty()) {
                return;
            }

            std::vector<uint8_t> packet(kVideoHeaderSize + vp8.size());
            write_u32_le(packet.data() + 0, static_cast<uint32_t>(frame.width));
            write_u32_le(packet.data() + 4, static_cast<uint32_t>(frame.height));
            write_u32_le(packet.data() + 8, now_ms());
            std::memcpy(packet.data() + kVideoHeaderSize, vp8.data(), vp8.size());

            transport_.send_video(packet.data(), packet.size());
        }))
    {
        LOG_ERROR() << "failed to start screen capture";
        video_encoder_.shutdown();
        return;
    }

    sharing_ = true;
    LOG_INFO() << "screen sharing started";
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
    // timestamp at offset 8 (unused for now)

    const uint8_t* vp8 = data + kVideoHeaderSize;
    size_t vp8_len = len - kVideoHeaderSize;

    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
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
        it = peer_video_.emplace(peer_id, std::move(vs)).first;
    }

    auto& vs = *it->second;
    if (vs.decoder.decode(vp8, vp8_len, vs.rgba, vs.width, vs.height)) {
        vs.dirty = true;
    }
}
