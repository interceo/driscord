#pragma once

#include "transport.hpp"

// Handles audio and screen-audio data channels.
// Must be constructed before Transport::connect() is called.
class AudioTransport {
public:
    using PacketCb  = Transport::PacketCb;
    using Callback  = std::function<void()>;

    explicit AudioTransport(Transport& transport);

    void send_audio(const uint8_t* data, size_t len);
    void send_screen_audio(const uint8_t* data, size_t len);

    void on_audio_received(PacketCb cb)               { on_audio_              = std::move(cb); }
    void on_screen_audio_received(PacketCb cb)        { on_screen_audio_       = std::move(cb); }
    void on_audio_channel_opened(Callback cb)         { on_audio_opened_       = std::move(cb); }
    void on_screen_audio_channel_opened(Callback cb)  { on_screen_audio_opened_ = std::move(cb); }

private:
    Transport& transport_;
    PacketCb   on_audio_;
    PacketCb   on_screen_audio_;
    Callback   on_audio_opened_;
    Callback   on_screen_audio_opened_;
};
