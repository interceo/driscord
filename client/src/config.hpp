#pragma once

#include <fmt/compile.h>
#include <fmt/format.h>

#include <string>
#include <vector>

struct TurnServer {
    std::string url;
    std::string user;
    std::string pass;
};

struct Config {
    std::string server_host = "localhost";
    int server_port = 8080;
    int screen_fps = 60;
    int capture_width = 1920;
    int capture_height = 1080;
    int video_bitrate_kbps = 8000;
    int voice_jitter_ms = 500;
    int screen_buffer_ms = 500;
    int max_sync_gap_ms = 2000;

    std::vector<TurnServer> turn_servers;

    std::string server_url() const { return fmt::format(FMT_COMPILE("ws://{}:{}"), server_host, server_port); }

    // Tries ./driscord.json, then ~/.config/driscord/config.json.
    // Missing file or missing keys silently use defaults.
    static Config load(const std::string& path);
    static Config load_default();
};
