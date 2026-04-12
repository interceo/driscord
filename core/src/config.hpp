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
    int gop_size = 30;
    int voice_bitrate_kbps = 64;
    int system_audio_bitrate_kbps = 128;
    float noise_gate_threshold = 0.01f;
    int voice_jitter_ms = 80;
    int screen_buffer_ms = 120;
    int max_sync_gap_ms = 2000;
    int hold_threshold_ms = 50;
    int drain_threshold_ms = 50;

    std::vector<TurnServer> turn_servers;
};
