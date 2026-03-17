#pragma once

#include "utils/opus_codec.hpp"
#include "utils/protocol.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class MaDevice;

class AudioSender {
public:
    static constexpr int kChannels = 1;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioSender();
    ~AudioSender();

    AudioSender(const AudioSender&) = delete;
    AudioSender& operator=(const AudioSender&) = delete;

    bool start(PacketCallback on_packet);
    void stop();
    bool running() const { return running_; }

    void set_muted(bool m) { muted_ = m; }
    bool muted() const noexcept { return muted_; }

    float input_level() const noexcept { return input_level_; }

private:
    void on_capture(const float* input, uint32_t frames);

    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<float> input_level_{0.0f};

    PacketCallback on_packet_;

    std::unique_ptr<OpusEncode> encoder_;
    std::unique_ptr<MaDevice> device_;

    std::vector<float> capture_buf_;
    size_t capture_pos_ = 0;

    std::vector<uint8_t> encode_buf_;
    uint64_t send_seq_ = 0;
};
