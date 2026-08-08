#pragma once

#include "channel_labels.hpp"
#include "identity.hpp"
#include "transport_fsm.hpp"
#include "utils/expected.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <rtc/rtc.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class TransportError {
    WebSocketCreateFailed,
};

enum class TransportConnectionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed,
};

struct TransportStats {
    TransportConnectionState state = TransportConnectionState::Closed;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    std::optional<int> rtt_ms;
};

class Transport {
public:
    using PacketCb = std::function<
        void(const std::string& peer_id, const uint8_t* data, size_t len)>;
    using PeerEventCb = std::function<void(const std::string& peer_id)>;
    using ChannelEventCb = std::function<void()>;

    struct ChannelSpec {
        channel::MediaChannel label;
        bool unordered = true;
        int max_retransmits = 0;
        PacketCb on_data;
        ChannelEventCb on_open;
        ChannelEventCb on_close;
    };

    Transport();
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    void register_channel(ChannelSpec spec);

    utils::Expected<void, TransportError> connect(const std::string& ws_url);
    void disconnect();

    bool connected() const { return ws_connected_; }
    std::string local_id() const
    {
        std::scoped_lock lk(ws_mutex_);
        return local_id_.value;
    }

    void on_connected(std::function<void()> cb)
    {
        ensure_callbacks_mutable_();
        on_connected_ = std::move(cb);
    }
    void on_disconnected(std::function<void()> cb)
    {
        ensure_callbacks_mutable_();
        on_disconnected_ = std::move(cb);
    }
    void on_peer_joined(PeerEventCb cb)
    {
        ensure_callbacks_mutable_();
        on_peer_joined_ = std::move(cb);
    }
    void on_peer_left(PeerEventCb cb)
    {
        ensure_callbacks_mutable_();
        on_peer_left_ = std::move(cb);
    }
    void on_streaming_started(PeerEventCb cb)
    {
        ensure_callbacks_mutable_();
        on_streaming_started_ = std::move(cb);
    }
    void on_streaming_stopped(PeerEventCb cb)
    {
        ensure_callbacks_mutable_();
        on_streaming_stopped_ = std::move(cb);
    }
    void on_watch_started(PeerEventCb cb)
    {
        ensure_callbacks_mutable_();
        on_watch_started_ = std::move(cb);
    }
    void on_watch_stopped(PeerEventCb cb)
    {
        ensure_callbacks_mutable_();
        on_watch_stopped_ = std::move(cb);
    }

    std::string peer_username(const std::string& peer_id) const;
    void on_peer_identity(std::function<void(const std::string&, const std::string&)> cb)
    {
        ensure_callbacks_mutable_();
        on_peer_identity_ = std::move(cb);
    }

    void send_streaming_start();
    void send_streaming_stop();
    void send_watch_start();
    void send_watch_stop();

    void send_on_channel(channel::MediaChannel label,
        const uint8_t* data,
        size_t len);

    void send_on_channel(channel::MediaChannel label, rtc::binary&& data);

    size_t channel_buffered_amount(channel::MediaChannel label) const;

    TransportStats stats() const;
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

    struct Fsm;

    void ensure_callbacks_mutable_() const
    {
        assert(!callbacks_frozen_
            && "register Transport callbacks before connect()");
    }

    void on_ws_message(const std::string& raw);
    void set_peer_identity_(driscord::PeerId peer_id, driscord::Username username);

    void create_server_connection();
    void close_server_connection();
    void handle_answer(const std::string& sdp);
    void handle_candidate(const std::string& candidate, const std::string& mid);
    void setup_channel(channel::MediaChannel label,
        std::shared_ptr<rtc::DataChannel> dc);
    void send_signal(const nlohmann::json& msg);
    std::shared_ptr<rtc::DataChannel> open_channel(
        channel::MediaChannel label) const;

    void post_event(transport_fsm::Event ev);

    mutable std::mutex ws_mutex_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> ws_connected_ { false };
    driscord::PeerId local_id_;
    std::string ws_url_;

    rtc::Configuration rtc_config_;

    mutable std::mutex pc_mutex_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::unordered_map<channel::MediaChannel, ChannelState> channels_;

    mutable std::mutex peers_mutex_;
    std::unordered_set<driscord::PeerId> peers_;

    std::vector<ChannelSpec> channel_specs_;
    std::optional<channel::MediaChannel> primary_channel_;

    bool callbacks_frozen_ = false;

    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;
    PeerEventCb on_peer_joined_;
    PeerEventCb on_peer_left_;
    PeerEventCb on_streaming_started_;
    PeerEventCb on_streaming_stopped_;
    PeerEventCb on_watch_started_;
    PeerEventCb on_watch_stopped_;

    mutable std::mutex identity_mutex_;
    std::unordered_map<driscord::PeerId, driscord::Username> peer_usernames_;
    std::function<void(const std::string&, const std::string&)> on_peer_identity_;

    void fsm_loop_();
    std::thread fsm_thread_;
    std::mutex fsm_mutex_; // guards fsm_queue_ only
    std::condition_variable fsm_cv_;
    std::deque<transport_fsm::Event> fsm_queue_;
    std::atomic<bool> stop_fsm_ { false };
    std::unique_ptr<Fsm> fsm_;

    std::mutex fsm_run_mutex_;

    mutable std::mutex stats_mutex_;
    TransportStats stats_cache_;
};
