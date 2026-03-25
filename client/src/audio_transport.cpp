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

bool AudioTransport::start() {
    if (!mixer_.start()) {
        return false;
    }
    return sender_.start([this](const uint8_t* d, size_t l) {
        send_audio(d, l);
    });
}

void AudioTransport::stop() {
    sender_.stop();
    mixer_.stop();
}

void AudioTransport::set_self_muted(bool m) {
    sender_.set_muted(m);
}

bool AudioTransport::self_muted() const {
    return sender_.muted();
}

float AudioTransport::input_level() const {
    return sender_.input_level();
}

void AudioTransport::set_master_volume(float v) {
    mixer_.set_output_volume(v);
}

float AudioTransport::master_volume() const {
    return mixer_.output_volume();
}

void AudioTransport::set_deafened(bool d) {
    mixer_.set_deafened(d);
}

bool AudioTransport::deafened() const {
    return mixer_.deafened();
}

float AudioTransport::output_level() const {
    return mixer_.output_level();
}

void AudioTransport::on_peer_joined(const std::string& peer_id, int jitter_ms) {
    auto recv = std::make_shared<AudioReceiver>(jitter_ms);
    {
        std::scoped_lock lk(recv_mutex_);
        voice_recv_[peer_id] = recv;
    }
    mixer_.add_source(std::move(recv));
}

void AudioTransport::on_peer_left(const std::string& peer_id) {
    std::shared_ptr<AudioReceiver> recv;
    {
        std::scoped_lock lk(recv_mutex_);
        auto it = voice_recv_.find(peer_id);
        if (it == voice_recv_.end()) {
            return;
        }
        recv = std::move(it->second);
        voice_recv_.erase(it);
    }
    mixer_.remove_source(recv);
}

void AudioTransport::set_peer_volume(const std::string& peer_id, float v) {
    std::scoped_lock lk(recv_mutex_);
    auto it = voice_recv_.find(peer_id);
    if (it != voice_recv_.end()) {
        it->second->set_volume(v);
    }
}

float AudioTransport::peer_volume(const std::string& peer_id) const {
    std::scoped_lock lk(recv_mutex_);
    auto it = voice_recv_.find(peer_id);
    return it != voice_recv_.end() ? it->second->volume() : 1.0f;
}

void AudioTransport::set_peer_muted(const std::string& peer_id, bool muted) {
    std::scoped_lock lk(recv_mutex_);
    auto it = voice_recv_.find(peer_id);
    if (it != voice_recv_.end()) {
        it->second->set_muted(muted);
    }
}

bool AudioTransport::peer_muted(const std::string& peer_id) const {
    std::scoped_lock lk(recv_mutex_);
    auto it = voice_recv_.find(peer_id);
    return it != voice_recv_.end() ? it->second->muted() : false;
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

void AudioTransport::set_screen_audio_peer_volume(const std::string& peer_id, float v) {
    std::scoped_lock lk(recv_mutex_);
    auto it = screen_audio_recv_.find(peer_id);
    if (it != screen_audio_recv_.end()) {
        it->second->set_volume(v);
    }
}

float AudioTransport::screen_audio_peer_volume(const std::string& peer_id) const {
    std::scoped_lock lk(recv_mutex_);
    auto it = screen_audio_recv_.find(peer_id);
    return it != screen_audio_recv_.end() ? it->second->volume() : 1.0f;
}

void AudioTransport::set_screen_audio_peer_muted(const std::string& peer_id, bool muted) {
    std::scoped_lock lk(recv_mutex_);
    auto it = screen_audio_recv_.find(peer_id);
    if (it != screen_audio_recv_.end()) {
        it->second->set_muted(muted);
    }
}

bool AudioTransport::screen_audio_peer_muted(const std::string& peer_id) const {
    std::scoped_lock lk(recv_mutex_);
    auto it = screen_audio_recv_.find(peer_id);
    return it != screen_audio_recv_.end() ? it->second->muted() : false;
}

void AudioTransport::add_screen_audio_to_mixer(const std::string& peer_id) {
    std::shared_ptr<AudioReceiver> recv;
    {
        std::scoped_lock lk(recv_mutex_);
        auto it = screen_audio_recv_.find(peer_id);
        if (it != screen_audio_recv_.end()) {
            recv = it->second;
        }
    }
    if (recv) {
        mixer_.add_source(std::move(recv));
    }
}

void AudioTransport::remove_screen_audio_from_mixer(const std::string& peer_id) {
    std::shared_ptr<AudioReceiver> recv;
    {
        std::scoped_lock lk(recv_mutex_);
        auto it = screen_audio_recv_.find(peer_id);
        if (it != screen_audio_recv_.end()) {
            recv = it->second;
        }
    }
    if (recv) {
        mixer_.remove_source(recv);
    }
}
