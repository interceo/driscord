#include "audio/audio.hpp"
#include "transport.hpp"
#include "utils/protocol.hpp"
#include "video/video_codec.hpp"

const char* to_string(AudioError e)
{
    switch (e) {
    case AudioError::OpusInitFailed:
        return "OpusInitFailed";
    case AudioError::SenderDeviceStartFailed:
        return "SenderDeviceStartFailed";
    case AudioError::MixerDeviceStartFailed:
        return "MixerDeviceStartFailed";
    }
    return "Unknown";
}

const char* to_string(TransportError e)
{
    switch (e) {
    case TransportError::WebSocketCreateFailed:
        return "WebSocketCreateFailed";
    }
    return "Unknown";
}

const char* to_string(VideoError e)
{
    switch (e) {
    case VideoError::InvalidDimensions:
        return "InvalidDimensions";
    case VideoError::InvalidFps:
        return "InvalidFps";
    case VideoError::InvalidBitrate:
        return "InvalidBitrate";
    case VideoError::EncoderInitFailed:
        return "EncoderInitFailed";
    case VideoError::VideoSenderFailed:
        return "VideoSenderFailed";
    case VideoError::CaptureStartFailed:
        return "CaptureStartFailed";
    }
    return "Unknown";
}

const char* protocol::to_string(protocol::VideoCodec c)
{
    switch (c) {
    case protocol::VideoCodec::H264:
        return "H264";
    case protocol::VideoCodec::HEVC:
        return "HEVC";
    }
    return "Unknown";
}
