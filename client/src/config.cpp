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

    return cfg;
}

Config Config::load_default() {
    if (fs::exists("driscord.json")) {
        return load("driscord.json");
    }

    if (const char* home = std::getenv("HOME")) {
        auto p = fs::path(home) / ".config" / "driscord" / "config.json";
        if (fs::exists(p)) {
            return load(p.string());
        }
    }

    LOG_INFO() << "no config file found, using defaults";
    return Config{};
}
