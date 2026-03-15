#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class AudioReceiver;
struct ma_device;

class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    bool start();
    void stop();
    bool running() const { return running_; }

    void add_source(AudioReceiver* src);
    void remove_source(AudioReceiver* src);

    void set_output_volume(float v) { output_volume_.store(v); }
    float output_volume() const { return output_volume_.load(); }

    void set_deafened(bool d) { deafened_.store(d); }
    bool deafened() const { return deafened_.load(); }

    float output_level() const { return output_level_.load(); }

private:
    void on_playback(float* output, uint32_t frames);

    std::mutex sources_mutex_;
    std::vector<AudioReceiver*> sources_;
    std::vector<AudioReceiver*> snapshot_;

    std::unique_ptr<ma_device> device_;
    std::atomic<bool> running_{false};
    std::atomic<float> output_volume_{1.0f};
    std::atomic<bool> deafened_{false};
    std::atomic<float> output_level_{0.0f};
};
