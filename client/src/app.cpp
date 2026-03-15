#include "app.hpp"

#include <algorithm>
#include <cstring>

#include "log.hpp"
#include "utils/byte_utils.hpp"

App::App(const Config& cfg) : config_(cfg) {
    for (auto& ts : cfg.turn_servers) {
        transport_.add_turn_server(ts.url, ts.user, ts.pass);
    }

    transport_.on_audio_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        audio_receiver_.feed_packet(peer_id, data, len, peer_volume(peer_id));
    });

    transport_.on_video_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        receiver_.push_video_packet(peer_id, data, len);
    });

    transport_.on_screen_audio_received([this](const std::string& /*peer_id*/, const uint8_t* data, size_t len) {
        receiver_.push_audio_packet(data, len);
    });

    transport_.on_peer_joined([](const std::string& peer_id) { LOG_INFO() << "peer joined: " << peer_id; });

    transport_.on_video_channel_opened([this]() {
        if (sender_.sharing()) {
            sender_.force_keyframe();
        }
    });

    transport_.on_keyframe_requested([this]() {
        if (sender_.sharing()) {
            LOG_INFO() << "keyframe requested by peer";
            sender_.force_keyframe();
        }
    });

    transport_.on_peer_left([this](const std::string& peer_id) {
        LOG_INFO() << "peer left: " << peer_id;
        audio_receiver_.remove_voice_peer(peer_id);
        if (receiver_.active_peer() == peer_id) {
            receiver_.reset();
            video_renderer_.remove_peer(peer_id);
            last_rendered_peer_.clear();
        }
        {
            std::scoped_lock lk(peer_vol_mutex_);
            peer_volumes_.erase(peer_id);
        }
    });

    receiver_.set_keyframe_callback([this]() { transport_.send_keyframe_request(); });
}

App::~App() { disconnect(); }

void App::update() {
    if (state_ == AppState::Connecting && transport_.connected()) {
        state_ = AppState::Connected;
        LOG_INFO() << "connected, id: " << transport_.local_id();

        audio_receiver_.set_screen_stream(receiver_.jitter());

        const bool sender_ok = audio_sender_.start([this](const uint8_t* data, size_t len) {
            transport_.send_audio(data, len);
        });
        const bool receiver_ok = audio_receiver_.start();

        if (!sender_ok || !receiver_ok) {
            LOG_ERROR() << "failed to start audio (sender=" << sender_ok << " receiver=" << receiver_ok << ")";
        }
    }

    if (auto* frame = receiver_.update()) {
        std::string peer = receiver_.active_peer();
        if (!peer.empty()) {
            if (!last_rendered_peer_.empty() && last_rendered_peer_ != peer) {
                video_renderer_.remove_peer(last_rendered_peer_);
            }
            video_renderer_.update_frame(peer, frame->rgba.data(), frame->width, frame->height);
            last_rendered_peer_ = peer;
        }
    }

    if (!last_rendered_peer_.empty() && !receiver_.active()) {
        video_renderer_.remove_peer(last_rendered_peer_);
        last_rendered_peer_.clear();
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
    audio_sender_.stop();
    audio_receiver_.stop();
    transport_.disconnect();
    state_ = AppState::Disconnected;
    receiver_.reset();
    last_rendered_peer_.clear();
    video_renderer_.cleanup();
}

void App::toggle_mute() { audio_sender_.set_muted(!audio_sender_.muted()); }

void App::toggle_deafen() {
    const bool new_deaf = !audio_receiver_.deafened();
    audio_receiver_.set_deafened(new_deaf);
    if (new_deaf) {
        audio_sender_.set_muted(true);
    }
}

void App::set_volume(float vol) { audio_receiver_.set_output_volume(vol); }

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
    if (sender_.sharing() || state_ != AppState::Connected) {
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

    audio_receiver_.set_screen_stream(receiver_.jitter());

    if (!sender_.start(
            target,
            max_w,
            max_h,
            fps,
            config_.video_bitrate_kbps,
            share_audio,
            [this](const uint8_t* data, size_t len) { transport_.send_video(data, len); },
            [this](const uint8_t* data, size_t len) { transport_.send_screen_audio(data, len); }
        ))
    {
        return;
    }

    clear_preview();
    audio_receiver_.set_sharing_screen_audio(sender_.sharing_audio());
}

void App::stop_sharing() {
    if (!sender_.sharing()) {
        return;
    }

    sender_.stop();
    audio_receiver_.set_sharing_screen_audio(false);
    receiver_.reset();
}

void App::update_preview(const CaptureTarget& target) {
    constexpr int kPreviewW = 384;
    constexpr int kPreviewH = 216;

    auto frame = ScreenCapture::grab_thumbnail(target, kPreviewW, kPreviewH);
    if (frame.data.empty()) {
        return;
    }

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

StreamStats App::stream_stats(const std::string& /*peer_id*/) const {
    StreamStats s;
    s.width = receiver_.width();
    s.height = receiver_.height();
    s.measured_kbps = receiver_.measured_kbps();
    return s;
}
