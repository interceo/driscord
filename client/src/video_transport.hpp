#pragma once

#include "transport.hpp"

// Handles the video data channel: video frames + keyframe negotiation.
// Must be constructed before Transport::connect() is called.
class VideoTransport {
public:
    using PacketCb = Transport::PacketCb;
    using Callback = std::function<void()>;

    explicit VideoTransport(Transport& transport);

    void send_video(const uint8_t* data, size_t len);
    void send_keyframe_request();

    void on_video_received(PacketCb cb)       { on_video_   = std::move(cb); }
    void on_video_channel_opened(Callback cb) { on_opened_  = std::move(cb); }
    void on_keyframe_requested(Callback cb)   { on_kf_req_  = std::move(cb); }

private:
    Transport& transport_;
    PacketCb   on_video_;
    Callback   on_opened_;
    Callback   on_kf_req_;
};
