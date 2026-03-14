#pragma once

#include <fmt/compile.h>
#include <fmt/format.h>

#include <string>

struct Config {
    std::string server_host = "localhost";
    int server_port = 8080;
    int screen_fps = 60;
    int capture_width = 1920;
    int capture_height = 1080;
    int video_bitrate_kbps = 4000;

    std::string server_url() const { return fmt::format(FMT_COMPILE("ws://{}:{}"), server_host, server_port); }

    // Tries ./driscord.json, then ~/.config/driscord/config.json.
    // Missing file or missing keys silently use defaults.
    static Config load(const std::string& path);
    static Config load_default();
};
