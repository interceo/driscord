#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "log.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

Config Config::load(const std::string& path) {
    Config cfg;

    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARNING() << "config not found: " << path << ", using defaults";
        return cfg;
    }

    try {
        auto j = json::parse(f);

        if (j.contains("server_host")) {
            cfg.server_host = j["server_host"].get<std::string>();
        }
        if (j.contains("server_port")) {
            cfg.server_port = j["server_port"].get<int>();
        }
        if (j.contains("screen_fps")) {
            cfg.screen_fps = j["screen_fps"].get<int>();
        }
        if (j.contains("capture_width")) {
            cfg.capture_width = j["capture_width"].get<int>();
        }
        if (j.contains("capture_height")) {
            cfg.capture_height = j["capture_height"].get<int>();
        }
        if (j.contains("video_bitrate_kbps")) {
            cfg.video_bitrate_kbps = j["video_bitrate_kbps"].get<int>();
        }
        if (j.contains("voice_jitter_ms")) {
            cfg.voice_jitter_ms = j["voice_jitter_ms"].get<int>();
        }
        if (j.contains("screen_buffer_ms")) {
            cfg.screen_buffer_ms = j["screen_buffer_ms"].get<int>();
        }
        // Single TURN server (legacy format)
        if (j.contains("turn_url") && j["turn_url"].is_string()) {
            TurnServer ts;
            ts.url = j["turn_url"].get<std::string>();
            if (j.contains("turn_user")) {
                ts.user = j["turn_user"].get<std::string>();
            }
            if (j.contains("turn_pass")) {
                ts.pass = j["turn_pass"].get<std::string>();
            }
            if (!ts.url.empty()) {
                cfg.turn_servers.push_back(std::move(ts));
            }
        }
        // Array of TURN servers
        if (j.contains("turn_servers") && j["turn_servers"].is_array()) {
            for (auto& entry : j["turn_servers"]) {
                TurnServer ts;
                ts.url = entry.value("url", "");
                ts.user = entry.value("user", "");
                ts.pass = entry.value("pass", "");
                if (!ts.url.empty()) {
                    cfg.turn_servers.push_back(std::move(ts));
                }
            }
        }

        LOG_INFO() << "config loaded from " << path;
    } catch (const json::exception& e) {
        LOG_ERROR() << "failed to parse config " << path << ": " << e.what();
    }

    if (cfg.server_port < 1 || cfg.server_port > 65535) {
        LOG_WARNING() << "invalid server_port " << cfg.server_port << ", using default 8080";
        cfg.server_port = 8080;
    }
    if (cfg.video_bitrate_kbps < 100 || cfg.video_bitrate_kbps > 100000) {
        LOG_WARNING() << "invalid video_bitrate_kbps " << cfg.video_bitrate_kbps << ", using default 8000";
        cfg.video_bitrate_kbps = 8000;
    }
    if (cfg.screen_fps < 1 || cfg.screen_fps > 240) {
        LOG_WARNING() << "invalid screen_fps " << cfg.screen_fps << ", using default 60";
        cfg.screen_fps = 60;
    }
    if (cfg.server_host.empty()) {
        LOG_WARNING() << "empty server_host, using default localhost";
        cfg.server_host = "localhost";
    }
    if (cfg.voice_jitter_ms < 0 || cfg.voice_jitter_ms > 500) {
        LOG_WARNING() << "invalid voice_jitter_ms " << cfg.voice_jitter_ms << ", using default 80";
        cfg.voice_jitter_ms = 80;
    }
    if (cfg.screen_buffer_ms < 0 || cfg.screen_buffer_ms > 500) {
        LOG_WARNING() << "invalid screen_buffer_ms " << cfg.screen_buffer_ms << ", using default 80";
        cfg.screen_buffer_ms = 80;
    }

    return cfg;
}

Config Config::load_default() {
    if (fs::exists("driscord.json")) {
        return load("driscord.json");
    }

    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
#endif
    if (home) {
        auto p = fs::path(home) / ".config" / "driscord" / "config.json";
        if (fs::exists(p)) {
            return load(p.string());
        }
    }

    LOG_INFO() << "no config file found, using defaults";
    return Config{};
}
