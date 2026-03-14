#pragma once

#include <string>
#include <vector>

#include "audio_engine.hpp"
#include "config.hpp"
#include "voice_transport.hpp"

enum class AppState {
    Disconnected,
    Connecting,
    Connected,
};

class App {
public:
    explicit App(const Config& cfg);
    ~App();

    void update();

    void connect(const std::string& server_url);
    void disconnect();
    void toggle_mute();
    void set_volume(float vol);

    AppState state() const { return state_; }
    bool muted() const { return audio_.muted(); }
    float volume() const { return audio_.output_volume(); }
    float input_level() const { return audio_.input_level(); }
    float output_level() const { return audio_.output_level(); }
    std::string local_id() const { return transport_.local_id(); }
    const Config& config() const noexcept { return config_; }

    struct PeerView {
        std::string id;
        bool connected;
    };
    std::vector<PeerView> peers() const;

private:
    Config config_;
    AppState state_ = AppState::Disconnected;

    AudioEngine audio_;
    VoiceTransport transport_;
};
