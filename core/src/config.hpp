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
    float noise_gate_threshold = 0.01f;
    int hold_threshold_ms = 50;
    int drain_threshold_ms = 50;

    std::vector<TurnServer> turn_servers;
};

namespace stream_defaults {
inline constexpr int kVoiceBitrateKbps = 64;
inline constexpr int kSystemAudioBitrateKbps = 128;
inline constexpr int kVoiceJitterMs = 80;
inline constexpr int kScreenBufferMs = 120;
inline constexpr int kMaxSyncGapMs = 2000;
}
