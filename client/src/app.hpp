#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio_engine.hpp"
#include "voice_transport.hpp"

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
    bool muted() const { return audio_.muted(); }
    float volume() const { return audio_.output_volume(); }
    float input_level() const { return audio_.input_level(); }
    float output_level() const { return audio_.output_level(); }
    std::string local_id() const { return transport_.local_id(); }
    const std::string& server_url() const noexcept { return server_url_; }

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

    AudioEngine audio_;
    VoiceTransport transport_;

    mutable std::mutex log_mutex_;
    std::vector<LogEntry> logs_;
};
