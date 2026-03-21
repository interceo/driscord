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
                std::scoped_lock lk(recv_mutex_);
                auto it = voice_recv_.find(peer_id);
                if (it != voice_recv_.end()) {
                    it->second->push_packet(utils::vector_view<const uint8_t>{data, len});
                }
            },
    });

    transport.register_channel({
        .label           = "screen_audio",
        .unordered       = true,
        .max_retransmits = 0,
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
                std::scoped_lock lk(recv_mutex_);
                auto it = screen_audio_recv_.find(peer_id);
                if (it != screen_audio_recv_.end()) {
                    it->second->push_packet(utils::vector_view<const uint8_t>{data, len});
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

void AudioTransport::register_voice(const std::string& peer_id, std::shared_ptr<AudioReceiver> recv) {
    std::scoped_lock lk(recv_mutex_);
    voice_recv_[peer_id] = std::move(recv);
}

void AudioTransport::unregister_voice(const std::string& peer_id) {
    std::scoped_lock lk(recv_mutex_);
    voice_recv_.erase(peer_id);
}

void AudioTransport::set_screen_audio_recv(
    const std::string& peer_id,
    std::shared_ptr<AudioReceiver> recv
) {
    std::scoped_lock lk(recv_mutex_);
    if (recv) {
        screen_audio_recv_[peer_id] = std::move(recv);
    } else {
        screen_audio_recv_.erase(peer_id);
    }
}

void AudioTransport::unset_screen_audio_recv(const std::string& peer_id) {
    std::scoped_lock lk(recv_mutex_);
    screen_audio_recv_.erase(peer_id);
}
