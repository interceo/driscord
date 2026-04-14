#pragma once

#include "audio/audio.hpp"
#include "transport.hpp"
#include "utils/protocol.hpp"
#include "video/video_codec.hpp"

const char* to_string(AudioError e);
const char* to_string(TransportError e);
const char* to_string(VideoError e);
const char* to_string(protocol::VideoCodec c);