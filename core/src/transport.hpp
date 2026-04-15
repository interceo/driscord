#pragma once

#include "utils/expected.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class TransportError {
    WebSocketCreateFailed,
};

class Transport {
public:
    using PacketCb = std::function<
        void(const std::string& peer_id, const uint8_t* data, size_t len)>;
    using PeerEventCb = std::function<void(const std::string& peer_id)>;

    struct ChannelSpec {
        std::string label;
        bool unordered = true;
        int max_retransmits = 0;
        PacketCb on_data;
        PeerEventCb on_open;
        PeerEventCb on_close;
    };

    Transport();
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    void register_channel(ChannelSpec spec);

    void add_turn_server(const std::string& url,
        const std::string& user,
        const std::string& pass);

    // Replace the entire ICE server list. Pass an empty vector to disable
    // STUN/TURN entirely (useful for tests on loopback where host candidates
    // are sufficient). Must be called before connect().
    void set_ice_servers(const std::vector<std::string>& urls);

    utils::Expected<void, TransportError> connect(const std::string& ws_url);
    void disconnect();

    bool connected() const { return ws_connected_; }
    std::string local_id() const
    {
        std::scoped_lock lk(ws_mutex_);
        return local_id_;
    }

    void on_peer_joined(PeerEventCb cb) { on_peer_joined_ = std::move(cb); }
    void on_peer_left(PeerEventCb cb) { on_peer_left_ = std::move(cb); }
    void on_streaming_started(PeerEventCb cb)
    {
        on_streaming_started_ = std::move(cb);
    }
    void on_streaming_stopped(PeerEventCb cb)
    {
        on_streaming_stopped_ = std::move(cb);
    }
    void on_watch_started(PeerEventCb cb) { on_watch_started_ = std::move(cb); }
    void on_watch_stopped(PeerEventCb cb) { on_watch_stopped_ = std::move(cb); }

    void send_streaming_start();
    void send_streaming_stop();
    void send_watch_start();
    void send_watch_stop();

    void send_on_channel(const std::string& label,
        const uint8_t* data,
        size_t len);
    void send_on_channel_to(const std::string& label,
        const std::string& peer_id,
        const uint8_t* data,
        size_t len);

    // Move-based overload: transfers ownership to libdatachannel, avoids internal
    // copy.
    void send_on_channel_to(const std::string& label,
        const std::string& peer_id,
        rtc::binary&& data);

    // Returns open DCs for label in the order of peer_ids, skipping closed ones.
    // Single lock acquisition — use for high-frequency multicast (e.g. video).
    std::vector<std::shared_ptr<rtc::DataChannel>> get_open_channels(
        const std::string& label,
        const std::vector<std::string>& peer_ids) const;

    // Per-peer stats snapshot: bytes sent/received, RTT, connection state.
    std::string stats_json() const;

    struct PeerInfo {
        std::string id;
        bool primary_open = false;
    };
    std::vector<PeerInfo> peers() const;

private:
    struct ChannelState {
        std::shared_ptr<rtc::DataChannel> dc;
        bool open = false;
    };

    struct PeerState {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::unordered_map<std::string, ChannelState> channels;
        bool was_offerer = false;
    };

    void on_ws_message(const std::string& raw);
    void create_peer(const std::string& peer_id, bool create_offer);
    void handle_offer(const std::string& from, const std::string& sdp);
    void handle_answer(const std::string& from, const std::string& sdp);
    void handle_candidate(const std::string& from,
        const std::string& candidate,
        const std::string& mid);
    void setup_channel(const std::string& peer_id,
        const std::string& label,
        std::shared_ptr<rtc::DataChannel> dc);
    void send_signal(const nlohmann::json& msg);

    mutable std::mutex ws_mutex_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> ws_connected_ { false };
    std::string local_id_;
    std::string ws_url_;

    rtc::Configuration rtc_config_;

    mutable std::mutex peers_mutex_;
    std::unordered_map<std::string, PeerState> peers_;

    std::vector<ChannelSpec> channel_specs_;
    std::string primary_channel_;

    PeerEventCb on_peer_joined_;
    PeerEventCb on_peer_left_;
    PeerEventCb on_streaming_started_;
    PeerEventCb on_streaming_stopped_;
    PeerEventCb on_watch_started_;
    PeerEventCb on_watch_stopped_;

    // Background timer: collects stats, drives ICE reconnect on failure.
    void timer_loop_();
    std::thread timer_thread_;
    std::mutex timer_cv_mutex_;
    std::condition_variable timer_cv_;
    std::atomic<bool> stop_timer_ { false };

    // Last reconnect attempt per peer (guarded by peers_mutex_).
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> reconnect_times_;

    mutable std::mutex stats_mutex_;
    std::string stats_json_cache_ { "[]" };
};
