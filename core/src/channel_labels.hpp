#pragma once

#include <optional>
#include <string_view>

namespace channel {

enum class MediaChannel {
    Audio,
    Video,
    ScreenAudio,
    Control,
};

inline constexpr std::string_view to_label(MediaChannel channel)
{
    switch (channel) {
    case MediaChannel::Audio:
        return "audio";
    case MediaChannel::Video:
        return "video";
    case MediaChannel::ScreenAudio:
        return "screen_audio";
    case MediaChannel::Control:
        return "control";
    }
    return "control";
}

inline constexpr std::optional<MediaChannel> parse_label(std::string_view label)
{
    if (label == "audio") {
        return MediaChannel::Audio;
    }
    if (label == "video") {
        return MediaChannel::Video;
    }
    if (label == "screen_audio") {
        return MediaChannel::ScreenAudio;
    }
    if (label == "control") {
        return MediaChannel::Control;
    }
    return std::nullopt;
}

inline constexpr bool is_watcher_gated(MediaChannel channel)
{
    return channel == MediaChannel::Video
        || channel == MediaChannel::ScreenAudio;
}

} // namespace channel
