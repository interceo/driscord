#pragma once

// Stream quality / FPS definitions shared between App and JNI bridge.

enum class StreamQuality : int {
    Source = 0,
    HD_720,
    FHD_1080,
    QHD_1440,
    Count,
};

struct StreamPreset {
    const char* label;
    int width;
    int height;
};

inline constexpr StreamPreset kStreamPresets[] = {
    {"Source", 0, 0},
    {"720p", 1280, 720},
    {"1080p", 1920, 1080},
    {"1440p", 2560, 1440},
};
static_assert(static_cast<int>(StreamQuality::Count) ==
              sizeof(kStreamPresets) / sizeof(kStreamPresets[0]));
inline constexpr int kStreamPresetCount = static_cast<int>(StreamQuality::Count);

enum class FrameRate : int {
    FPS_15 = 0,
    FPS_30,
    FPS_60,
    Count,
};

inline constexpr int kFpsValues[] = {15, 30, 60};
static_assert(static_cast<int>(FrameRate::Count) ==
              sizeof(kFpsValues) / sizeof(kFpsValues[0]));
inline constexpr int kFpsOptionCount = static_cast<int>(FrameRate::Count);

inline constexpr int fps_value(FrameRate fr) { return kFpsValues[static_cast<int>(fr)]; }
