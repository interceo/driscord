#pragma once

#include "identity.hpp"
#include "utils/expected.hpp"
#include "utils/signaling_protocol.hpp"

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <rtc/rtc.hpp>
#include <string>
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

// WebSocket signaling only. Media PeerConnections live in GoogleWebRtcClient;
// the former libdatachannel PeerConnection/DataChannel transport was removed.
class Transport final {
public:
    using PeerEventCb = std::function<void(const std::string& peer_id)>;
    using ConnectionEventCb = std::function<void(bool connected)>;
    using MediaAnswerCb = std::function<void(
        signaling::ConnectionId connection, const std::string& sdp)>;
    using MediaCandidateCb = std::function<void(signaling::ConnectionId connection,
        const std::string& candidate,
        const std::string& sdp_mid)>;
    using MediaTrackBindingCb = std::function<void(
        signaling::ConnectionId connection,
        const std::string& sdp_mid,
        const std::optional<driscord::PeerId>& peer_id)>;
    using WatchRejectedCb = std::function<void(const std::string& peer_id,
        signaling::WatchRejectReason reason)>;

    Transport() = default;
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    utils::Expected<void, TransportError> connect(const std::string& ws_url);
    void disconnect();

    [[nodiscard]] bool connected() const noexcept { return ws_connected_; }
    [[nodiscard]] std::string local_id() const;

    void on_connected(std::function<void()> cb);
    void on_disconnected(std::function<void()> cb);
    void add_connection_listener(ConnectionEventCb cb);
    void on_peer_joined(PeerEventCb cb);
    void on_peer_left(PeerEventCb cb);
    void on_streaming_started(PeerEventCb cb);
    void on_streaming_stopped(PeerEventCb cb);
    void on_media_answer(MediaAnswerCb cb);
    void add_media_answer_listener(MediaAnswerCb cb);
    void on_media_candidate(MediaCandidateCb cb);
    void add_media_candidate_listener(MediaCandidateCb cb);
    void on_media_track_binding(MediaTrackBindingCb cb);
    void add_media_track_binding_listener(MediaTrackBindingCb cb);
    void on_watch_rejected(WatchRejectedCb cb);
    void add_watch_rejected_listener(WatchRejectedCb cb);
    void on_peer_identity(
        std::function<void(const std::string&, const std::string&)> cb);

    [[nodiscard]] std::string peer_username(
        const std::string& peer_id) const;

    void send_streaming_start();
    void send_streaming_stop();
    void send_watch_start(const std::string& peer_id);
    void send_watch_stop(const std::string& peer_id);
    void send_media_offer(signaling::ConnectionId connection,
        const std::string& sdp);
    void send_media_candidate(signaling::ConnectionId connection,
        const std::string& candidate,
        const std::string& sdp_mid);

    [[nodiscard]] TransportStats stats() const;
    [[nodiscard]] std::string stats_json() const;

    struct PeerInfo {
        std::string id;
    };
    [[nodiscard]] std::vector<PeerInfo> peers() const;

private:
    void ensure_callbacks_mutable_() const
    {
        assert(!callbacks_frozen_
            && "register Transport callbacks before connect()");
    }
    void on_ws_message(const std::string& raw);
    void set_peer_identity_(driscord::PeerId peer_id,
        driscord::Username username);
    void send_signal(const nlohmann::json& message);
    void update_state(TransportConnectionState state);
    void clear_peers();
    [[nodiscard]] bool is_current_socket(
        const std::shared_ptr<rtc::WebSocket>& socket) const;

    mutable std::mutex ws_mutex_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> ws_connected_ { false };
    driscord::PeerId local_id_;
    bool callbacks_frozen_ = false;

    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;
    std::vector<ConnectionEventCb> connection_listeners_;
    PeerEventCb on_peer_joined_;
    PeerEventCb on_peer_left_;
    PeerEventCb on_streaming_started_;
    PeerEventCb on_streaming_stopped_;
    MediaAnswerCb on_media_answer_;
    MediaCandidateCb on_media_candidate_;
    MediaTrackBindingCb on_media_track_binding_;
    WatchRejectedCb on_watch_rejected_;
    std::vector<MediaAnswerCb> media_answer_listeners_;
    std::vector<MediaCandidateCb> media_candidate_listeners_;
    std::vector<MediaTrackBindingCb> media_track_binding_listeners_;
    std::vector<WatchRejectedCb> watch_rejected_listeners_;

    mutable std::mutex peers_mutex_;
    std::unordered_set<driscord::PeerId> peers_;
    mutable std::mutex identity_mutex_;
    std::unordered_map<driscord::PeerId, driscord::Username> peer_usernames_;
    std::function<void(const std::string&, const std::string&)>
        on_peer_identity_;

    mutable std::mutex stats_mutex_;
    TransportStats stats_;
};
