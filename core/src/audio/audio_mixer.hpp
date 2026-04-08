#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class AudioReceiver;
class MaDevice;

class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    AudioMixer(const AudioMixer&)            = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // Returns JSON array of {id, name} for all playback devices.
    static std::string list_output_devices_json();

    bool start();
    void stop();
    bool running() const { return running_; }

    // Set the playback device by name (empty = default). If already running,
    // restarts the device immediately without clearing sources.
    void set_output_device(std::string id);

    void add_source(std::shared_ptr<AudioReceiver> src);
    void remove_source(const std::shared_ptr<AudioReceiver>& src);

    void set_output_volume(float v) { output_volume_.store(v); }
    float output_volume() const { return output_volume_.load(); }

    void set_deafened(bool d) { deafened_.store(d); }
    bool deafened() const { return deafened_.load(); }

    float output_level() const { return output_level_.load(); }

private:
    void on_playback(float* output, uint32_t frames);

    std::mutex sources_mutex_;
    std::vector<std::shared_ptr<AudioReceiver>> sources_;
    std::vector<std::shared_ptr<AudioReceiver>> snapshot_;

    std::string       output_device_id_; // empty = default device
    std::unique_ptr<MaDevice> device_;
    std::atomic<bool> running_{false};
    std::atomic<float> output_volume_{1.0f};
    std::atomic<bool> deafened_{false};
    std::atomic<float> output_level_{0.0f};
    uint64_t playback_count_ = 0;
};
