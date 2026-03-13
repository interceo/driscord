#include "app.hpp"

#include <iostream>

App::App()
    : audio_(std::make_unique<AudioEngine>())
    , transport_(std::make_unique<VoiceTransport>()) {

    transport_->on_audio_received(
        [this](const std::string& /*peer_id*/, const uint8_t* data, size_t len) {
            audio_->feed_packet(data, len);
        });

    transport_->on_peer_joined([this](const std::string& peer_id) {
        log(LogEntry::Info, "Peer joined: " + peer_id.substr(0, 8));
    });

    transport_->on_peer_left([this](const std::string& peer_id) {
        log(LogEntry::Info, "Peer left: " + peer_id.substr(0, 8));
    });
}

App::~App() {
    disconnect();
}

void App::update() {
    if (state_ == AppState::Connecting && transport_->connected()) {
        state_ = AppState::Connected;
        log(LogEntry::Info, "Connected. ID: " + transport_->local_id().substr(0, 8));

        bool ok = audio_->start(
            [this](const uint8_t* data, size_t len) {
                transport_->send_audio(data, len);
            });

        if (!ok) {
            log(LogEntry::Error, "Failed to start audio engine");
        } else {
            log(LogEntry::Info, "Audio engine started");
        }
    }
}

void App::connect(const std::string& server_url) {
    if (state_ != AppState::Disconnected) return;
    server_url_ = server_url;
    state_ = AppState::Connecting;
    log(LogEntry::Info, "Connecting to " + server_url + "...");
    transport_->connect(server_url);
}

void App::disconnect() {
    audio_->stop();
    transport_->disconnect();
    state_ = AppState::Disconnected;
    log(LogEntry::Info, "Disconnected");
}

void App::toggle_mute() {
    if (audio_) {
        audio_->set_muted(!audio_->muted());
    }
}

void App::set_volume(float vol) {
    if (audio_) {
        audio_->set_output_volume(vol);
    }
}

std::vector<App::PeerView> App::peers() const {
    std::vector<PeerView> result;
    if (transport_) {
        for (auto& p : transport_->peers()) {
            result.push_back({p.id, p.dc_open});
        }
    }
    return result;
}

std::vector<LogEntry> App::recent_logs() const {
    std::scoped_lock lk(log_mutex_);
    size_t start = logs_.size() > 50 ? logs_.size() - 50 : 0;
    return {logs_.begin() + static_cast<long>(start), logs_.end()};
}

void App::log(LogEntry::Level lvl, const std::string& text) {
    std::scoped_lock lk(log_mutex_);
    logs_.push_back({lvl, text});
    if (lvl == LogEntry::Error) {
        std::cerr << "[ERROR] " << text << std::endl;
    } else {
        std::cout << "[INFO] " << text << std::endl;
    }
}
