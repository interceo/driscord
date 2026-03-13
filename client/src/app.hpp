#pragma once

#include "audio_engine.hpp"
#include "voice_transport.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class AppState {
    Disconnected,
    Connecting,
    Connected,
};

struct LogEntry {
    enum Level { Info, Error };
    Level level;
    std::string text;
};

class App {
public:
    App();
    ~App();

    void update();

    // UI actions
    void connect(const std::string& server_url);
    void disconnect();
    void toggle_mute();
    void set_volume(float vol);

    // State for UI
    AppState state() const { return state_; }
    bool muted() const { return audio_ ? audio_->muted() : false; }
    float volume() const { return audio_ ? audio_->output_volume() : 1.0f; }
    float input_level() const { return audio_ ? audio_->input_level() : 0.0f; }
    float output_level() const { return audio_ ? audio_->output_level() : 0.0f; }
    std::string local_id() const { return transport_ ? transport_->local_id() : ""; }
    std::string server_url() const { return server_url_; }

    struct PeerView {
        std::string id;
        bool connected;
    };
    std::vector<PeerView> peers() const;

    std::vector<LogEntry> recent_logs() const;

private:
    void log(LogEntry::Level lvl, const std::string& text);

    AppState state_ = AppState::Disconnected;
    std::string server_url_ = "ws://localhost:8080";

    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<VoiceTransport> transport_;

    mutable std::mutex log_mutex_;
    std::vector<LogEntry> logs_;
};
