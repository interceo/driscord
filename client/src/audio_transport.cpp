#include "audio_transport.hpp"

#include "log.hpp"

AudioTransport::AudioTransport(Transport& transport)
    : transport_(transport) {
    auto recv_count = std::make_shared<std::atomic<uint64_t>>(0);

    transport.register_channel({
        .label           = "audio",
        .unordered       = true,
        .max_retransmits = 0,
        .on_data =
            [this, recv_count](const std::string& peer_id, const uint8_t* data, size_t len) {
                const uint64_t n = ++(*recv_count);
                if (n == 1 || n % 200 == 0) {
                    LOG_INFO() << "[audio-dc] rx#" << n << " peer=" << peer_id << " bytes=" << len;
                }
                if (on_audio_) {
                    on_audio_(peer_id, data, len);
                }
            },
        .on_open =
            [this](const std::string& /*peer_id*/) {
                if (on_audio_opened_) {
                    on_audio_opened_();
                }
            },
    });

    transport.register_channel({
        .label           = "screen_audio",
        .unordered       = true,
        .max_retransmits = 0,
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
                if (on_screen_audio_) {
                    on_screen_audio_(peer_id, data, len);
                }
            },
        .on_open =
            [this](const std::string& /*peer_id*/) {
                if (on_screen_audio_opened_) {
                    on_screen_audio_opened_();
                }
            },
    });
}

void AudioTransport::send_audio(const uint8_t* data, size_t len) {
    transport_.send_on_channel("audio", data, len);
}

void AudioTransport::send_screen_audio(const uint8_t* data, size_t len) {
    transport_.send_on_channel("screen_audio", data, len);
}
