#include "transport.hpp"

#include "log.hpp"
#include "match.hpp"
#include "system_ca_bundle.hpp"

#include <chrono>
#include <type_traits>
#include <variant>

using json = nlohmann::json;

namespace {

const char* to_string(TransportConnectionState state)
{
    switch (state) {
    case TransportConnectionState::New:
        return "new";
    case TransportConnectionState::Connecting:
        return "connecting";
    case TransportConnectionState::Connected:
        return "connected";
    case TransportConnectionState::Disconnected:
        return "disconnected";
    case TransportConnectionState::Failed:
        return "failed";
    case TransportConnectionState::Closed:
        return "closed";
    }
    return "closed";
}

} // namespace

Transport::~Transport()
{
    disconnect();
}

utils::Expected<void, TransportError> Transport::connect(
    const std::string& ws_url)
{
    disconnect();
    callbacks_frozen_ = true;
    update_state(TransportConnectionState::Connecting);

    std::shared_ptr<rtc::WebSocket> ws;
    try {
        rtc::WebSocket::Configuration config;
        config.pingInterval = std::chrono::seconds(15);
        config.maxOutstandingPings = 2;
        // libdatachannel's MbedTLS backend (Windows) verifies wss peers only
        // against an explicitly supplied chain; backends that read the system
        // trust store themselves report no bundle here.
        if (auto ca_bundle = utils::system_ca_bundle_pem()) {
            config.caCertificatePemFile = std::move(*ca_bundle);
        }
        ws = std::make_shared<rtc::WebSocket>(config);
    } catch (const std::exception& exception) {
        LOG_ERROR() << "Transport: WebSocket create failed: "
                    << exception.what();
        update_state(TransportConnectionState::Failed);
        return utils::Unexpected(TransportError::WebSocketCreateFailed);
    }

    const std::weak_ptr<rtc::WebSocket> weak_ws = ws;
    const auto disconnect_notified = std::make_shared<std::atomic<bool>>(false);
    const auto notify_disconnected = [this, disconnect_notified] {
        if (disconnect_notified->exchange(true)) {
            return;
        }
        clear_peers();
        for (const auto& listener : connection_listeners_) {
            if (listener) {
                listener(false);
            }
        }
        if (on_disconnected_) {
            on_disconnected_();
        }
    };
    ws->onOpen([this, weak_ws, ws_url] {
        const auto socket = weak_ws.lock();
        if (!socket || !is_current_socket(socket)) {
            return;
        }
        LOG_INFO() << "ws connected to " << ws_url;
        ws_connected_ = true;
        update_state(TransportConnectionState::Connected);
        for (const auto& listener : connection_listeners_) {
            if (listener) {
                listener(true);
            }
        }
        if (on_connected_) {
            on_connected_();
        }
    });
    ws->onClosed([this, weak_ws, notify_disconnected] {
        const auto socket = weak_ws.lock();
        if (!socket || !is_current_socket(socket)) {
            return;
        }
        const bool was_connected = ws_connected_.exchange(false);
        {
            std::scoped_lock lock(ws_mutex_);
            if (ws_ == socket) {
                ws_.reset();
                local_id_.value.clear();
            }
        }
        update_state(TransportConnectionState::Disconnected);
        LOG_INFO() << (was_connected ? "ws disconnected"
                                     : "ws connection closed before opening");
        notify_disconnected();
    });
    ws->onError([this, weak_ws, notify_disconnected](std::string error) {
        const auto socket = weak_ws.lock();
        if (!socket || !is_current_socket(socket)) {
            return;
        }
        LOG_ERROR() << "ws error: " << error;
        ws_connected_ = false;
        {
            std::scoped_lock lock(ws_mutex_);
            if (ws_ == socket) {
                local_id_.value.clear();
            }
        }
        update_state(TransportConnectionState::Failed);
        notify_disconnected();
    });
    ws->onMessage([this, weak_ws](auto message) {
        const auto socket = weak_ws.lock();
        if (!socket || !is_current_socket(socket)) {
            return;
        }
        if (auto* text = std::get_if<std::string>(&message)) {
            {
                std::scoped_lock lock(stats_mutex_);
                stats_.bytes_received += text->size();
            }
            on_ws_message(*text);
        }
    });

    {
        std::scoped_lock lock(ws_mutex_);
        ws_ = ws;
    }
    ws->open(ws_url);
    return { };
}

void Transport::disconnect()
{
    std::shared_ptr<rtc::WebSocket> ws;
    const bool was_connected = ws_connected_.exchange(false);
    {
        std::scoped_lock lock(ws_mutex_);
        ws = std::move(ws_);
        local_id_.value.clear();
    }
    clear_peers();
    if (ws) {
        ws->close();
    }
    update_state(TransportConnectionState::Closed);
    if (was_connected) {
        for (const auto& listener : connection_listeners_) {
            if (listener) {
                listener(false);
            }
        }
        if (on_disconnected_) {
            on_disconnected_();
        }
    }
}

std::string Transport::local_id() const
{
    std::scoped_lock lock(ws_mutex_);
    return local_id_.value;
}

#define DRISCORD_SET_CALLBACK(method, member, type) \
    void Transport::method(type callback)           \
    {                                               \
        ensure_callbacks_mutable_();                \
        member = std::move(callback);               \
    }

DRISCORD_SET_CALLBACK(on_connected, on_connected_, std::function<void()>)
DRISCORD_SET_CALLBACK(on_disconnected, on_disconnected_, std::function<void()>)
DRISCORD_SET_CALLBACK(on_peer_joined, on_peer_joined_, PeerEventCb)
DRISCORD_SET_CALLBACK(on_peer_left, on_peer_left_, PeerEventCb)
DRISCORD_SET_CALLBACK(
    on_streaming_started, on_streaming_started_, PeerEventCb)
DRISCORD_SET_CALLBACK(
    on_streaming_stopped, on_streaming_stopped_, PeerEventCb)
DRISCORD_SET_CALLBACK(on_media_answer, on_media_answer_, MediaAnswerCb)
DRISCORD_SET_CALLBACK(on_media_candidate, on_media_candidate_, MediaCandidateCb)
DRISCORD_SET_CALLBACK(on_media_track_binding, on_media_track_binding_,
    MediaTrackBindingCb)
DRISCORD_SET_CALLBACK(
    on_watch_rejected, on_watch_rejected_, WatchRejectedCb)

#undef DRISCORD_SET_CALLBACK

void Transport::add_connection_listener(ConnectionEventCb callback)
{
    ensure_callbacks_mutable_();
    connection_listeners_.push_back(std::move(callback));
}

void Transport::add_media_answer_listener(MediaAnswerCb callback)
{
    ensure_callbacks_mutable_();
    media_answer_listeners_.push_back(std::move(callback));
}

void Transport::add_media_candidate_listener(MediaCandidateCb callback)
{
    ensure_callbacks_mutable_();
    media_candidate_listeners_.push_back(std::move(callback));
}

void Transport::add_media_track_binding_listener(
    MediaTrackBindingCb callback)
{
    ensure_callbacks_mutable_();
    media_track_binding_listeners_.push_back(std::move(callback));
}

void Transport::add_watch_rejected_listener(WatchRejectedCb callback)
{
    ensure_callbacks_mutable_();
    watch_rejected_listeners_.push_back(std::move(callback));
}

void Transport::on_peer_identity(
    std::function<void(const std::string&, const std::string&)> callback)
{
    ensure_callbacks_mutable_();
    on_peer_identity_ = std::move(callback);
}

void Transport::update_state(TransportConnectionState state)
{
    std::scoped_lock lock(stats_mutex_);
    stats_.state = state;
}

void Transport::clear_peers()
{
    {
        std::scoped_lock lock(peers_mutex_);
        peers_.clear();
    }
    {
        std::scoped_lock lock(identity_mutex_);
        peer_usernames_.clear();
    }
}

bool Transport::is_current_socket(
    const std::shared_ptr<rtc::WebSocket>& socket) const
{
    std::scoped_lock lock(ws_mutex_);
    return ws_ == socket;
}

TransportStats Transport::stats() const
{
    std::scoped_lock lock(stats_mutex_);
    return stats_;
}

std::string Transport::stats_json() const
{
    const auto snapshot = stats();
    return json {
        { "state", to_string(snapshot.state) },
        { "bytes_sent", snapshot.bytes_sent },
        { "bytes_received", snapshot.bytes_received },
        { "rtt_ms", snapshot.rtt_ms.value_or(-1) },
    }
        .dump();
}

std::vector<Transport::PeerInfo> Transport::peers() const
{
    std::scoped_lock lock(peers_mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto& peer : peers_) {
        result.push_back({ peer.value });
    }
    return result;
}

std::string Transport::peer_username(const std::string& peer_id) const
{
    std::scoped_lock lock(identity_mutex_);
    const auto found = peer_usernames_.find(driscord::PeerId { peer_id });
    return found == peer_usernames_.end() ? "" : found->second.value;
}

void Transport::set_peer_identity_(
    driscord::PeerId peer_id, driscord::Username username)
{
    if (username.value.empty()) {
        return;
    }
    std::function<void(const std::string&, const std::string&)> callback;
    {
        std::scoped_lock lock(identity_mutex_);
        peer_usernames_[peer_id] = username;
        callback = on_peer_identity_;
    }
    if (callback) {
        callback(peer_id.value, username.value);
    }
}

void Transport::on_ws_message(const std::string& raw)
{
    static_assert(std::variant_size_v<signaling::Message> == 12,
        "signaling::Message changed; update Transport::on_ws_message");
    auto parsed = signaling::parse(raw);
    if (!parsed) {
        LOG_ERROR() << "on_ws_message: " << signaling::to_string(parsed.error());
        return;
    }

    utils::Match(parsed.value(), [this](const signaling::Welcome& message) {
            {
                std::scoped_lock lock(ws_mutex_);
                local_id_ = message.id;
            }
            for (const auto& peer : message.peers) {
                set_peer_identity_(peer.id, peer.username);
                {
                    std::scoped_lock lock(peers_mutex_);
                    peers_.insert(peer.id);
                }
                if (on_peer_joined_) {
                    on_peer_joined_(peer.id.value);
                }
            }
            for (const auto& peer : message.streaming_peers) {
                if (on_streaming_started_) {
                    on_streaming_started_(peer.value);
                }
            } }, [this](const signaling::PeerJoined& message) {
            set_peer_identity_(message.id, message.username);
            {
                std::scoped_lock lock(peers_mutex_);
                peers_.insert(message.id);
            }
            if (on_peer_joined_) {
                on_peer_joined_(message.id.value);
            } }, [this](const signaling::PeerLeft& message) {
            {
                std::scoped_lock lock(peers_mutex_);
                peers_.erase(message.id);
            }
            {
                std::scoped_lock lock(identity_mutex_);
                peer_usernames_.erase(message.id);
            }
            if (on_peer_left_) {
                on_peer_left_(message.id.value);
            } }, [this](const signaling::Answer& message) {
            if (on_media_answer_) {
                on_media_answer_(message.connection, message.sdp);
            }
            for (const auto& listener : media_answer_listeners_) {
                if (listener) {
                    listener(message.connection, message.sdp);
                }
            } }, [this](const signaling::Candidate& message) {
            if (on_media_candidate_) {
                on_media_candidate_(message.connection, message.candidate,
                    message.sdp_mid);
            }
            for (const auto& listener : media_candidate_listeners_) {
                if (listener) {
                    listener(message.connection, message.candidate,
                        message.sdp_mid);
                }
            } }, [this](const signaling::TrackBinding& message) {
            if (on_media_track_binding_) {
                on_media_track_binding_(message.connection, message.sdp_mid,
                    message.peer_id);
            }
            for (const auto& listener : media_track_binding_listeners_) {
                if (listener) {
                    listener(message.connection, message.sdp_mid,
                        message.peer_id);
                }
            } }, [this](const signaling::StreamingStart& message) {
            if (message.from && on_streaming_started_) {
                on_streaming_started_(message.from->value);
            } }, [this](const signaling::StreamingStop& message) {
            if (message.from && on_streaming_stopped_) {
                on_streaming_stopped_(message.from->value);
            } }, [this](const signaling::WatchRejected& message) {
            if (on_watch_rejected_) {
                on_watch_rejected_(message.peer_id.value, message.reason);
            }
            for (const auto& listener : watch_rejected_listeners_) {
                if (listener) {
                    listener(message.peer_id.value, message.reason);
                }
            } }, [](const signaling::WatchStart&) { }, [](const signaling::WatchStop&) { }, [](const signaling::Offer&) { LOG_WARNING() << "ignoring offer from signaling server"; });
}

void Transport::send_streaming_start()
{
    send_signal(signaling::encode(signaling::StreamingStart { }));
}

void Transport::send_streaming_stop()
{
    send_signal(signaling::encode(signaling::StreamingStop { }));
}

void Transport::send_watch_start(const std::string& peer_id)
{
    send_signal(signaling::encode(
        signaling::WatchStart { driscord::PeerId { peer_id } }));
}

void Transport::send_watch_stop(const std::string& peer_id)
{
    send_signal(signaling::encode(
        signaling::WatchStop { driscord::PeerId { peer_id } }));
}

void Transport::send_media_offer(
    signaling::ConnectionId connection, const std::string& sdp)
{
    send_signal(signaling::encode(signaling::Offer { sdp, connection }));
}

void Transport::send_media_candidate(signaling::ConnectionId connection,
    const std::string& candidate,
    const std::string& sdp_mid)
{
    send_signal(signaling::encode(
        signaling::Candidate { candidate, sdp_mid, connection }));
}

void Transport::send_signal(const json& message)
{
    const std::string payload = message.dump();
    std::scoped_lock lock(ws_mutex_);
    if (!ws_ || !ws_connected_) {
        return;
    }
    try {
        ws_->send(payload);
        std::scoped_lock stats_lock(stats_mutex_);
        stats_.bytes_sent += payload.size();
    } catch (const std::exception& exception) {
        LOG_ERROR() << "WebSocket send failed: " << exception.what();
    }
}
