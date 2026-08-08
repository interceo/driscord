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

// Owns the client's two links to the signaling server:
//   - a WebSocket carrying JSON signaling (room roster, identity, stream
//     lifecycle) and the SDP/ICE exchange;
//   - a single PeerConnection to that same server, whose DataChannels carry
//     all media.
//
// There is no peer-to-peer mesh: media goes client → server → clients, and the
// server decides who receives what. Because the server is the answerer and is
// publicly reachable, no STUN or TURN is involved — the client always dials out.
class Transport {
public:
    // Media arriving from another peer, relayed by the server. `peer_id` is
    // the originating peer, not the server.
    using PacketCb = std::function<
        void(const std::string& peer_id, const uint8_t* data, size_t len)>;
    using PeerEventCb = std::function<void(const std::string& peer_id)>;
    // Channels are per-connection (to the server), not per-peer, so their
    // open/close carries no peer id.
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

    // Must be called before connect() — the channel set is created when the
    // PeerConnection is built.
    void register_channel(ChannelSpec spec);

    utils::Expected<void, TransportError> connect(const std::string& ws_url);
    void disconnect();

    bool connected() const { return ws_connected_; }
    std::string local_id() const
    {
        std::scoped_lock lk(ws_mutex_);
        return local_id_.value;
    }

    // Callback registration. These are read on the WebSocket/RTC threads once
    // connect() has run, with no lock — so they may only be set beforehand,
    // while this object is still owned by the constructing thread. Assigning one
    // after connect() would race the reader; ensure_callbacks_mutable_() traps
    // that misuse in debug builds.
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

    // Identity: usernames arrive from the signaling server (welcome/peer_joined
    // carry them, sourced from the `u=` query param on connect).
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

    // Send media to the server, which fans it out to the room. Who receives it
    // is the server's decision (see watch_start/watch_stop for screen share).
    void send_on_channel(channel::MediaChannel label,
        const uint8_t* data,
        size_t len);

    // Move-based overload: transfers ownership to libdatachannel, avoids an
    // internal copy on the hot video path.
    void send_on_channel(channel::MediaChannel label, rtc::binary&& data);

    // Bytes queued but not yet handed to the network on this channel. Used for
    // backpressure: when the uplink can't keep up, dropping a frame beats
    // growing an unbounded queue and with it the latency.
    size_t channel_buffered_amount(channel::MediaChannel label) const;

    // Stats for the connection to the server: bytes sent/received, RTT, state.
    TransportStats stats() const;
    std::string stats_json() const;

    struct PeerInfo {
        std::string id;
        // True once our media path to the server is up, i.e. we can actually
        // exchange audio with this peer.
        bool primary_open = false;
    };
    std::vector<PeerInfo> peers() const;

private:
    struct ChannelState {
        std::shared_ptr<rtc::DataChannel> dc;
        bool open = false;
    };

    // Owns the SML state machine. Pimpl'd so <boost/sml.hpp> — and the
    // transition table's template soup — stays out of this public header.
    struct Fsm;

    // Trips if a callback is registered after connect() has frozen them. The
    // flag is written and read only on the owning thread (setters + connect),
    // never on the network threads that consume the callbacks.
    void ensure_callbacks_mutable_() const
    {
        assert(!callbacks_frozen_
            && "register Transport callbacks before connect()");
    }

    void on_ws_message(const std::string& raw);
    void set_peer_identity_(driscord::PeerId peer_id, driscord::Username username);
    // Builds the PeerConnection to the server, creates the registered
    // channels and sends the offer. Only ever called by the state machine,
    // on the FSM thread.
    void create_server_connection();
    void close_server_connection();
    void handle_answer(const std::string& sdp);
    void handle_candidate(const std::string& candidate, const std::string& mid);
    void setup_channel(channel::MediaChannel label,
        std::shared_ptr<rtc::DataChannel> dc);
    void send_signal(const nlohmann::json& msg);
    std::shared_ptr<rtc::DataChannel> open_channel(
        channel::MediaChannel label) const;

    // Hands an event to the state machine. Callable from any thread: the event
    // is queued and replayed on the FSM thread, which is the only place the
    // machine and the PeerConnection lifecycle are ever touched.
    void post_event(transport_fsm::Event ev);

    mutable std::mutex ws_mutex_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> ws_connected_ { false };
    driscord::PeerId local_id_;
    std::string ws_url_;

    rtc::Configuration rtc_config_;

    // The single connection to the server and its channels.
    mutable std::mutex pc_mutex_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::unordered_map<channel::MediaChannel, ChannelState> channels_;

    // Room roster, driven purely by signaling.
    mutable std::mutex peers_mutex_;
    std::unordered_set<driscord::PeerId> peers_;

    std::vector<ChannelSpec> channel_specs_;
    std::optional<channel::MediaChannel> primary_channel_;

    // Set once by connect(); afterwards the callbacks below are read-only and
    // safe to touch from the network threads without a lock.
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

    // The one thread that owns the state machine. It drains the event queue,
    // ticks the reconnect backoff and refreshes the stats snapshot. Every
    // PeerConnection create/close happens here and nowhere else, which is what
    // makes the machine safe without any locking of its own.
    void fsm_loop_();
    std::thread fsm_thread_;
    std::mutex fsm_mutex_; // guards fsm_queue_ only
    std::condition_variable fsm_cv_;
    std::deque<transport_fsm::Event> fsm_queue_;
    std::atomic<bool> stop_fsm_ { false };
    std::unique_ptr<Fsm> fsm_;

    // Held while the FSM thread is running transitions, and by disconnect()
    // while it tears down. Without it a queued event could build a new
    // PeerConnection just after disconnect() dismantled the old one, leaving a
    // live connection whose callbacks outlive this Transport.
    std::mutex fsm_run_mutex_;

    mutable std::mutex stats_mutex_;
    TransportStats stats_cache_;
};
