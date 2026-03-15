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
        auto* recv = get_or_create_voice(peer_id);
        recv->push_packet(data, len);
    });

    transport_.on_video_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        screen_session_.push_video_packet(peer_id, data, len);
    });

    transport_.on_screen_audio_received([this](const std::string& /*peer_id*/, const uint8_t* data, size_t len) {
        screen_session_.push_audio_packet(data, len);
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
        {
            std::scoped_lock lk(voice_mutex_);
            auto it = voice_receivers_.find(peer_id);
            if (it != voice_receivers_.end()) {
                audio_mixer_.remove_source(it->second.get());
                voice_receivers_.erase(it);
            }
        }
        if (screen_session_.active_peer() == peer_id) {
            screen_session_.reset();
            video_renderer_.remove_peer(peer_id);
            last_rendered_peer_.clear();
        }
        {
            std::scoped_lock lk(peer_vol_mutex_);
            peer_volumes_.erase(peer_id);
        }
    });

    screen_session_.set_keyframe_callback([this]() { transport_.send_keyframe_request(); });
}

App::~App() { disconnect(); }

void App::update() {
    if (state_ == AppState::Connecting && transport_.connected()) {
        state_ = AppState::Connected;
        LOG_INFO() << "connected, id: " << transport_.local_id();

        audio_mixer_.add_source(screen_session_.audio_receiver());

        const bool sender_ok = audio_sender_.start([this](const uint8_t* data, size_t len) {
            transport_.send_audio(data, len);
        });
        const bool mixer_ok = audio_mixer_.start();

        if (!sender_ok || !mixer_ok) {
            LOG_ERROR() << "failed to start audio (sender=" << sender_ok << " mixer=" << mixer_ok << ")";
        }
    }

    if (auto* frame = screen_session_.update()) {
        std::string peer = screen_session_.active_peer();
        if (!peer.empty()) {
            if (!last_rendered_peer_.empty() && last_rendered_peer_ != peer) {
                video_renderer_.remove_peer(last_rendered_peer_);
            }
            last_frame_w_ = frame->width;
            last_frame_h_ = frame->height;
            video_renderer_.update_frame(peer, frame->rgba.data(), frame->width, frame->height);
            last_rendered_peer_ = peer;
        }
    }

    if (!last_rendered_peer_.empty() && !screen_session_.active()) {
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
    audio_mixer_.stop();
    transport_.disconnect();
    state_ = AppState::Disconnected;
    {
        std::scoped_lock lk(voice_mutex_);
        voice_receivers_.clear();
    }
    screen_session_.reset();
    last_rendered_peer_.clear();
    video_renderer_.cleanup();
}

void App::toggle_mute() { audio_sender_.set_muted(!audio_sender_.muted()); }

void App::toggle_deafen() {
    const bool new_deaf = !audio_mixer_.deafened();
    audio_mixer_.set_deafened(new_deaf);
    if (new_deaf) {
        audio_sender_.set_muted(true);
    }
}

void App::set_volume(float vol) { audio_mixer_.set_output_volume(vol); }

void App::set_peer_volume(const std::string& peer_id, float vol) {
    {
        std::scoped_lock lk(peer_vol_mutex_);
        peer_volumes_[peer_id] = vol;
    }
    std::scoped_lock lk(voice_mutex_);
    auto it = voice_receivers_.find(peer_id);
    if (it != voice_receivers_.end()) {
        it->second->set_volume(vol);
    }
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

    if (sender_.sharing_audio()) {
        audio_mixer_.remove_source(screen_session_.audio_receiver());
    }
}

void App::stop_sharing() {
    if (!sender_.sharing()) {
        return;
    }

    const bool was_sharing_audio = sender_.sharing_audio();
    sender_.stop();

    if (was_sharing_audio) {
        audio_mixer_.add_source(screen_session_.audio_receiver());
    }

    screen_session_.reset();
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
    s.width = last_frame_w_;
    s.height = last_frame_h_;
    s.measured_kbps = screen_session_.measured_kbps();

    const auto vs = screen_session_.video_stats();
    const auto as = screen_session_.audio_stats();
    s.jitter = {
        .video_queue = static_cast<int>(vs.queue_size),
        .video_buf_ms = static_cast<int>(vs.buffered_ms),
        .video_drops = vs.drop_count,
        .video_misses = vs.miss_count,
        .audio_queue = static_cast<int>(as.queue_size),
        .audio_buf_ms = static_cast<int>(as.buffered_ms),
        .audio_drops = as.drop_count,
        .audio_misses = as.miss_count,
    };
    return s;
}

AudioReceiver* App::get_or_create_voice(const std::string& peer_id) {
    std::scoped_lock lk(voice_mutex_);
    auto& slot = voice_receivers_[peer_id];
    if (!slot) {
        slot = std::make_unique<AudioReceiver>(config_.voice_jitter_ms);
        float vol = peer_volume(peer_id);
        if (vol != 1.0f) {
            slot->set_volume(vol);
        }
        audio_mixer_.add_source(slot.get());
    }
    return slot.get();
}
