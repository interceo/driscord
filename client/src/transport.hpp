#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Core signaling + peer connection manager.
//
// Channels are registered via register_channel() before connect().
// The first registered channel is the "primary" — its open state is
// reflected in PeerInfo::primary_open.
class Transport {
public:
    using PacketCb    = std::function<void(const std::string& peer_id, const uint8_t* data, size_t len)>;
    using PeerEventCb = std::function<void(const std::string& peer_id)>;

    struct ChannelSpec {
        std::string label;
        bool        unordered       = true;
        int         max_retransmits = 0;   // 0 = drop immediately, -1 = reliable
        PacketCb    on_data;
        PeerEventCb on_open;
        PeerEventCb on_close;
    };

    Transport();
    ~Transport();

    Transport(const Transport&)            = delete;
    Transport& operator=(const Transport&) = delete;

    // Must be called before connect().
    void register_channel(ChannelSpec spec);

    void add_turn_server(const std::string& url, const std::string& user, const std::string& pass);
    void connect(const std::string& ws_url);
    void disconnect();

    bool        connected() const { return ws_connected_; }
    std::string local_id()  const { return local_id_; }

    void on_peer_joined(PeerEventCb cb) { on_peer_joined_ = std::move(cb); }
    void on_peer_left(PeerEventCb cb)   { on_peer_left_   = std::move(cb); }

    // Send data on a named channel to all peers that have it open.
    void send_on_channel(const std::string& label, const uint8_t* data, size_t len);

    struct PeerInfo {
        std::string id;
        bool        primary_open = false;
    };
    std::vector<PeerInfo> peers() const;

private:
    struct ChannelState {
        std::shared_ptr<rtc::DataChannel> dc;
        bool open = false;
    };

    struct PeerState {
        std::shared_ptr<rtc::PeerConnection>          pc;
        std::unordered_map<std::string, ChannelState> channels;
    };

    void on_ws_message(const std::string& raw);
    void create_peer(const std::string& peer_id, bool create_offer);
    void handle_offer(const std::string& from, const std::string& sdp);
    void handle_answer(const std::string& from, const std::string& sdp);
    void handle_candidate(const std::string& from, const std::string& candidate, const std::string& mid);
    void setup_channel(const std::string& peer_id, const std::string& label,
                       std::shared_ptr<rtc::DataChannel> dc);
    void send_signal(const nlohmann::json& msg);

    mutable std::mutex ws_mutex_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> ws_connected_{false};
    std::string local_id_;
    std::string ws_url_;

    rtc::Configuration rtc_config_;

    mutable std::mutex peers_mutex_;
    std::unordered_map<std::string, PeerState> peers_;

    std::vector<ChannelSpec> channel_specs_;
    std::string primary_channel_;

    PeerEventCb on_peer_joined_;
    PeerEventCb on_peer_left_;
};
