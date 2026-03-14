#include "app.hpp"

#include "log.hpp"

App::App(const Config& cfg) : config_(cfg) {
    transport_.on_audio_received([this](const std::string& /*peer_id*/, const uint8_t* data, size_t len) {
        audio_.feed_packet(data, len);
    });

    transport_.on_peer_joined([](const std::string& peer_id) { LOG_INFO() << "peer joined: " << peer_id; });

    transport_.on_peer_left([](const std::string& peer_id) { LOG_INFO() << "peer left: " << peer_id; });
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
    audio_.stop();
    transport_.disconnect();
    state_ = AppState::Disconnected;
}

void App::toggle_mute() { audio_.set_muted(!audio_.muted()); }

void App::set_volume(float vol) { audio_.set_output_volume(vol); }

std::vector<App::PeerView> App::peers() const {
    std::vector<PeerView> result;
    auto ps = transport_.peers();
    result.reserve(ps.size());
    for (auto& p : ps) {
        result.emplace_back(p.id, p.dc_open);
    }
    return result;
}
