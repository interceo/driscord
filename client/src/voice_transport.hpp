#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <string>
#include <unordered_map>

class VoiceTransport {
public:
    using AudioPacketCb = std::function<void(const std::string& peer_id, const uint8_t* data, size_t len)>;
    using VideoPacketCb = std::function<void(const std::string& peer_id, const uint8_t* data, size_t len)>;
    using PeerEventCb = std::function<void(const std::string& peer_id)>;

    VoiceTransport();
    ~VoiceTransport();

    void add_turn_server(const std::string& url, const std::string& user, const std::string& pass);

    VoiceTransport(const VoiceTransport&) = delete;
    VoiceTransport& operator=(const VoiceTransport&) = delete;

    void connect(const std::string& ws_url);
    void disconnect();

    bool connected() const { return ws_connected_; }
    std::string local_id() const { return local_id_; }

    void send_audio(const uint8_t* data, size_t len);
    void send_video(const uint8_t* data, size_t len);

    void on_audio_received(AudioPacketCb cb) { on_audio_ = std::move(cb); }
    void on_video_received(VideoPacketCb cb) { on_video_ = std::move(cb); }
    void on_peer_joined(PeerEventCb cb) { on_peer_joined_ = std::move(cb); }
    void on_peer_left(PeerEventCb cb) { on_peer_left_ = std::move(cb); }

    struct PeerInfo {
        std::string id;
        bool dc_open = false;
    };
    std::vector<PeerInfo> peers() const;

private:
    struct PeerState {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        std::shared_ptr<rtc::DataChannel> video_dc;
        bool dc_open = false;
        bool video_dc_open = false;
    };

    void on_ws_message(const std::string& raw);
    void create_peer(const std::string& peer_id, bool create_offer);
    void handle_offer(const std::string& from, const std::string& sdp);
    void handle_answer(const std::string& from, const std::string& sdp);
    void handle_candidate(const std::string& from, const std::string& candidate, const std::string& mid);
    void setup_audio_channel(const std::string& peer_id, std::shared_ptr<rtc::DataChannel> dc);
    void setup_video_channel(const std::string& peer_id, std::shared_ptr<rtc::DataChannel> dc);
    void send_signal(const nlohmann::json& msg);

    mutable std::mutex ws_mutex_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> ws_connected_{false};
    std::string local_id_;
    std::string ws_url_;

    rtc::Configuration rtc_config_;

    mutable std::mutex peers_mutex_;
    std::unordered_map<std::string, PeerState> peers_;

    AudioPacketCb on_audio_;
    VideoPacketCb on_video_;
    PeerEventCb on_peer_joined_;
    PeerEventCb on_peer_left_;
};
