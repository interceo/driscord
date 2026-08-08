#include "transport.hpp"

#include "log.hpp"
#include "protocol.hpp"
#include "signaling_protocol.hpp"
#include "transport_fsm_table.hpp"

#include <cstring>
#include <type_traits>

using json = nlohmann::json;

namespace {
int64_t steady_now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

// Bridges the state machine's Actions interface onto the Transport, and owns
// the machine itself. Defined here so <boost/sml.hpp> never reaches
// transport.hpp.
struct Transport::Fsm : transport_fsm::Actions {
    explicit Fsm(Transport& t)
        : owner(t)
        , machine(static_cast<transport_fsm::Actions&>(*this), backoff)
    {
    }

    void create_peer_connection() override { owner.create_server_connection(); }
    void apply_answer(const std::string& sdp) override { owner.handle_answer(sdp); }
    void apply_candidate(const std::string& candidate,
        const std::string& mid) override
    {
        owner.handle_candidate(candidate, mid);
    }
    void close_peer_connection() override { owner.close_server_connection(); }
    void log_ignored(const char* what) override
    {
        LOG_WARNING() << "ignoring out-of-phase " << what;
    }

    Transport& owner;
    transport_fsm::Backoff backoff;
    boost::sml::sm<transport_fsm::Machine> machine;
};

Transport::Transport()
{
    // No ICE servers: the server is publicly reachable and answers, so its
    // host candidates suffice and the client only ever dials out. Nothing here
    // needs STUN, and TURN is not part of this architecture at all.
    // Video frames are sent whole and fragmented by SCTP; 256 KB is a generous
    // ceiling for a single encoded frame.
    rtc_config_.maxMessageSize = 256 * 1024;

    fsm_ = std::make_unique<Fsm>(*this);
    fsm_thread_ = std::thread(&Transport::fsm_loop_, this);
}

Transport::~Transport()
{
    disconnect();

    stop_fsm_ = true;
    fsm_cv_.notify_all();
    if (fsm_thread_.joinable()) {
        fsm_thread_.join();
    }
}

void Transport::post_event(transport_fsm::Event ev)
{
    {
        std::scoped_lock lk(fsm_mutex_);
        if (stop_fsm_) {
            return;
        }
        fsm_queue_.push_back(std::move(ev));
    }
    fsm_cv_.notify_all();
}

void Transport::register_channel(ChannelSpec spec)
{
    if (!primary_channel_) {
        primary_channel_ = spec.label;
    }
    channel_specs_.push_back(std::move(spec));
}

utils::Expected<void, TransportError> Transport::connect(const std::string& ws_url)
{
    disconnect();
    ws_url_ = ws_url;

    // From here the network threads may read the callback set, so it must stop
    // changing. Latches on first connect; a reconnect keeps it frozen.
    callbacks_frozen_ = true;

    std::shared_ptr<rtc::WebSocket> ws;
    try {
        rtc::WebSocket::Configuration ws_config;
        ws_config.pingInterval = std::chrono::seconds(15);
        ws_config.maxOutstandingPings = 2;
        ws = std::make_shared<rtc::WebSocket>(ws_config);
    } catch (const std::exception& ex) {
        LOG_ERROR() << "Transport: WebSocket create failed: " << ex.what();
        return utils::Unexpected(TransportError::WebSocketCreateFailed);
    }

    ws->onOpen([this]() {
        LOG_INFO() << "ws connected to " << ws_url_;
        ws_connected_ = true;
        // Hand off to the state machine: it owns when the offer goes out.
        post_event(transport_fsm::WsOpened { });
        if (on_connected_)
            on_connected_();
    });

    ws->onClosed([this]() {
        LOG_INFO() << "ws disconnected";
        ws_connected_ = false;
        post_event(transport_fsm::WsClosed { });
        if (on_disconnected_)
            on_disconnected_();
    });

    ws->onError([](std::string error) { LOG_ERROR() << "ws error: " << error; });

    ws->onMessage([this](auto msg) {
        if (auto* str = std::get_if<std::string>(&msg)) {
            on_ws_message(*str);
        }
    });

    // Publish before opening: onOpen fires on a libdatachannel thread and the
    // offer that follows goes out through send_signal(), which would find ws_
    // still empty and silently drop it.
    {
        std::scoped_lock lk(ws_mutex_);
        ws_ = ws;
    }
    post_event(transport_fsm::ConnectRequested { });
    ws->open(ws_url);
    return { };
}

void Transport::create_server_connection()
{
    // A Tick queued before disconnect() could otherwise resurrect the media
    // path after the caller already tore everything down.
    if (!ws_connected_) {
        LOG_WARNING() << "skipping peer connection setup: signaling is down";
        return;
    }

    // Defensive: the machine always closes before it re-creates, but a stray
    // connection here would leak ICE ports and leave stale channel entries.
    close_server_connection();

    std::shared_ptr<rtc::PeerConnection> pc;
    try {
        pc = std::make_shared<rtc::PeerConnection>(rtc_config_);
    } catch (const std::exception& e) {
        LOG_ERROR() << "peer connection create failed: " << e.what();
        return;
    }

    pc->onLocalDescription([this](rtc::Description desc) {
        const auto type = desc.typeString();
        if (type == "offer") {
            send_signal(signaling::encode(signaling::Offer { std::string(desc) }));
        } else if (type == "answer") {
            send_signal(signaling::encode(signaling::Answer { std::string(desc) }));
        }
    });

    pc->onLocalCandidate([this](rtc::Candidate cand) {
        send_signal(signaling::encode(
            signaling::Candidate { std::string(cand), cand.mid() }));
    });

    pc->onStateChange([this](rtc::PeerConnection::State state) {
        LOG_INFO() << "server connection state: " << static_cast<int>(state);
        switch (state) {
        case rtc::PeerConnection::State::Connected:
            post_event(transport_fsm::PcConnected { });
            break;
        case rtc::PeerConnection::State::Failed:
            // Disconnected is transient — ICE may still recover on its own, so
            // only a hard Failed is worth tearing the connection down for.
            post_event(transport_fsm::PcFailed { steady_now_ms() });
            break;
        default:
            break;
        }
    });

    {
        std::scoped_lock lk(pc_mutex_);
        pc_ = pc;
        channels_.clear();
    }

    // The client is always the offerer and owns the channel set.
    for (const auto& spec : channel_specs_) {
        rtc::DataChannelInit init;
        init.reliability.unordered = spec.unordered;
        if (spec.max_retransmits >= 0) {
            init.reliability.maxRetransmits = spec.max_retransmits;
        }
        try {
            auto dc = pc->createDataChannel(
                std::string(channel::to_label(spec.label)), init);
            setup_channel(spec.label, std::move(dc));
        } catch (const std::exception& e) {
            LOG_ERROR() << "createDataChannel[" << channel::to_label(spec.label)
                        << "]: " << e.what();
        }
    }
}

void Transport::handle_answer(const std::string& sdp)
{
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::scoped_lock lk(pc_mutex_);
        pc = pc_;
    }
    if (!pc) {
        LOG_WARNING() << "answer with no peer connection, dropping";
        return;
    }
    try {
        pc->setRemoteDescription(
            rtc::Description(sdp, rtc::Description::Type::Answer));
    } catch (const std::exception& e) {
        LOG_ERROR() << "setRemoteDescription: " << e.what();
    }
}

void Transport::handle_candidate(const std::string& candidate,
    const std::string& mid)
{
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::scoped_lock lk(pc_mutex_);
        pc = pc_;
    }
    if (!pc) {
        LOG_WARNING() << "candidate with no peer connection, dropping";
        return;
    }
    try {
        pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    } catch (const std::exception& e) {
        LOG_ERROR() << "addRemoteCandidate: " << e.what();
    }
}

void Transport::setup_channel(channel::MediaChannel label,
    std::shared_ptr<rtc::DataChannel> dc)
{
    PacketCb on_data;
    ChannelEventCb on_open_cb;
    ChannelEventCb on_close_cb;
    bool found = false;

    for (const auto& spec : channel_specs_) {
        if (spec.label == label) {
            on_data = spec.on_data;
            on_open_cb = spec.on_open;
            on_close_cb = spec.on_close;
            found = true;
            break;
        }
    }

    const auto label_text = channel::to_label(label);
    if (!found) {
        LOG_WARNING() << "unknown channel label '" << label_text << "'";
        return;
    }

    dc->onOpen([this, label, label_text, on_open_cb]() {
        LOG_INFO() << "'" << label_text << "' channel open";
        {
            std::scoped_lock lk(pc_mutex_);
            channels_[label].open = true;
        }
        if (on_open_cb) {
            on_open_cb();
        }
    });

    dc->onClosed([this, label, label_text, on_close_cb]() {
        LOG_INFO() << "'" << label_text << "' channel closed";
        {
            std::scoped_lock lk(pc_mutex_);
            channels_[label].open = false;
        }
        if (on_close_cb) {
            on_close_cb();
        }
    });

    // Every media message is prefixed by the server with the id of the peer
    // that sent it.
    dc->onMessage([on_data, label_text](auto msg) {
        auto* data = std::get_if<rtc::binary>(&msg);
        if (!data || !on_data) {
            return;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(data->data());
        const size_t len = data->size();
        auto packet = protocol::decode_relayed_media(bytes, len);
        if (!packet) {
            LOG_WARNING() << "malformed media packet on '" << label_text << "' ("
                          << len << " bytes)";
            return;
        }
        on_data(packet->sender_id.value, packet->payload, packet->payload_len);
    });

    dc->onError([label_text](std::string error) {
        LOG_ERROR() << "'" << label_text << "' dc error: " << error;
    });

    std::scoped_lock lk(pc_mutex_);
    channels_[label].dc = std::move(dc);
}

std::shared_ptr<rtc::DataChannel> Transport::open_channel(
    channel::MediaChannel label) const
{
    std::scoped_lock lk(pc_mutex_);
    auto it = channels_.find(label);
    if (it == channels_.end() || !it->second.dc || !it->second.open) {
        return nullptr;
    }
    return it->second.dc;
}

void Transport::close_server_connection()
{
    std::shared_ptr<rtc::PeerConnection> pc;
    std::unordered_map<channel::MediaChannel, ChannelState> channels;
    {
        std::scoped_lock lk(pc_mutex_);
        pc = std::move(pc_);
        channels = std::move(channels_);
        pc_.reset();
        channels_.clear();
    }
    for (auto& [_, ch] : channels) {
        if (ch.dc) {
            ch.dc->close();
        }
    }
    if (pc) {
        pc->close();
    }
}

void Transport::disconnect()
{
    // Tell the machine to stop trying to reconnect, then tear down here rather
    // than waiting on the FSM thread: callers expect the transport to be down
    // by the time this returns. close_server_connection() is idempotent, so the
    // machine repeating it later is harmless.
    post_event(transport_fsm::DisconnectRequested { });

    // Drop the socket first. create_server_connection() refuses to run without
    // it, so from here on the machine cannot build a new connection even if a
    // stale event is still queued.
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::scoped_lock lk(ws_mutex_);
        ws = std::move(ws_);
        ws_connected_ = false;
    }

    // Wait out any transition already in flight — otherwise it could finish
    // building a connection right after the teardown below.
    std::scoped_lock run(fsm_run_mutex_);

    close_server_connection();

    {
        std::scoped_lock lk(peers_mutex_);
        peers_.clear();
    }
    {
        std::scoped_lock lk(identity_mutex_);
        peer_usernames_.clear();
    }

    if (ws) {
        ws->close();
    }
    local_id_.value.clear();
}

void Transport::send_on_channel(channel::MediaChannel label,
    const uint8_t* data,
    size_t len)
{
    auto dc = open_channel(label);
    if (!dc) {
        return;
    }
    try {
        dc->send(reinterpret_cast<const std::byte*>(data), len);
    } catch (const std::exception& e) {
        LOG_ERROR() << "send_on_channel[" << channel::to_label(label)
                    << "]: " << e.what();
    }
}

void Transport::send_on_channel(channel::MediaChannel label, rtc::binary&& data)
{
    auto dc = open_channel(label);
    if (!dc) {
        return;
    }
    try {
        dc->send(std::move(data));
    } catch (const std::exception& e) {
        LOG_ERROR() << "send_on_channel[" << channel::to_label(label)
                    << "]: " << e.what();
    }
}

size_t Transport::channel_buffered_amount(channel::MediaChannel label) const
{
    auto dc = open_channel(label);
    if (!dc) {
        return 0;
    }
    try {
        return dc->bufferedAmount();
    } catch (const std::exception&) {
        return 0;
    }
}

std::string Transport::stats_json() const
{
    std::scoped_lock lk(stats_mutex_);
    return stats_json_cache_;
}

void Transport::fsm_loop_()
{
    // Ticks drive the reconnect backoff, so they need to be finer-grained than
    // the old stats interval; stats are refreshed on a slower counter below.
    constexpr auto kTickInterval = std::chrono::milliseconds(250);
    constexpr int kTicksPerStatsRefresh = 8; // ~2 s, as before
    int ticks_since_stats = 0;

    while (true) {
        std::deque<transport_fsm::Event> batch;
        {
            std::unique_lock lk(fsm_mutex_);
            fsm_cv_.wait_for(lk, kTickInterval,
                [this] { return stop_fsm_.load() || !fsm_queue_.empty(); });
            batch.swap(fsm_queue_);
        }

        {
            // Excludes disconnect() for the duration, so teardown can never
            // interleave with a transition that is building a connection.
            std::scoped_lock run(fsm_run_mutex_);

            // Drain queued events first, then tick — so a PcFailed that arrived
            // in this batch arms the backoff before the tick that could act on
            // it.
            for (auto& ev : batch) {
                std::visit([this](auto&& e) { fsm_->machine.process_event(e); },
                    ev);
            }

            if (!stop_fsm_) {
                fsm_->machine.process_event(
                    transport_fsm::Tick { steady_now_ms() });
            }
        }

        if (stop_fsm_) {
            break;
        }

        if (++ticks_since_stats < kTicksPerStatsRefresh) {
            continue;
        }
        ticks_since_stats = 0;

        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::scoped_lock lk(pc_mutex_);
            pc = pc_;
        }

        json stats = json::object();
        if (pc) {
            switch (pc->state()) {
            case rtc::PeerConnection::State::New:
                stats["state"] = "new";
                break;
            case rtc::PeerConnection::State::Connecting:
                stats["state"] = "connecting";
                break;
            case rtc::PeerConnection::State::Connected:
                stats["state"] = "connected";
                break;
            case rtc::PeerConnection::State::Disconnected:
                stats["state"] = "disconnected";
                break;
            case rtc::PeerConnection::State::Failed:
                stats["state"] = "failed";
                break;
            case rtc::PeerConnection::State::Closed:
                stats["state"] = "closed";
                break;
            }
            stats["bytes_sent"] = pc->bytesSent();
            stats["bytes_received"] = pc->bytesReceived();
            const auto rtt = pc->rtt();
            stats["rtt_ms"] = rtt ? static_cast<int>(rtt->count()) : -1;
        } else {
            stats["state"] = "closed";
            stats["rtt_ms"] = -1;
        }

        std::scoped_lock lk(stats_mutex_);
        stats_json_cache_ = stats.dump();
    }
}

std::vector<Transport::PeerInfo> Transport::peers() const
{
    bool media_up = false;
    {
        std::scoped_lock lk(pc_mutex_);
        if (primary_channel_) {
            auto it = channels_.find(*primary_channel_);
            media_up = it != channels_.end() && it->second.open;
        }
    }

    std::scoped_lock lk(peers_mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto& id : peers_) {
        result.emplace_back(id.value, media_up);
    }
    return result;
}

std::string Transport::peer_username(const std::string& peer_id) const
{
    std::scoped_lock lk(identity_mutex_);
    auto it = peer_usernames_.find(driscord::PeerId { peer_id });
    return it != peer_usernames_.end() ? it->second.value : "";
}

void Transport::set_peer_identity_(
    driscord::PeerId peer_id,
    driscord::Username username)
{
    if (username.value.empty()) {
        return;
    }
    std::function<void(const std::string&, const std::string&)> cb;
    {
        std::scoped_lock lk(identity_mutex_);
        peer_usernames_[peer_id] = username;
        cb = on_peer_identity_;
    }
    if (cb) {
        cb(peer_id.value, username.value);
    }
}

void Transport::on_ws_message(const std::string& raw)
{
    auto parsed = signaling::parse(raw);
    if (!parsed) {
        LOG_ERROR() << "on_ws_message: "
                    << signaling::to_string(parsed.error());
        return;
    }

    std::visit([this](auto&& message) {
        using T = std::decay_t<decltype(message)>;

        if constexpr (std::is_same_v<T, signaling::Welcome>) {
            {
                std::scoped_lock lk(ws_mutex_);
                local_id_ = message.id;
            }
            LOG_INFO() << "assigned id: " << message.id.value;

            for (const auto& peer : message.peers) {
                set_peer_identity_(peer.id, peer.username);
                {
                    std::scoped_lock lk(peers_mutex_);
                    peers_.insert(peer.id);
                }
                if (on_peer_joined_) {
                    on_peer_joined_(peer.id.value);
                }
            }

            for (const auto& id : message.streaming_peers) {
                if (on_streaming_started_) {
                    on_streaming_started_(id.value);
                }
            }
        } else if constexpr (std::is_same_v<T, signaling::PeerJoined>) {
            LOG_INFO() << "peer joined: " << message.id.value;
            set_peer_identity_(message.id, message.username);
            {
                std::scoped_lock lk(peers_mutex_);
                peers_.insert(message.id);
            }
            if (on_peer_joined_) {
                on_peer_joined_(message.id.value);
            }
        } else if constexpr (std::is_same_v<T, signaling::PeerLeft>) {
            LOG_INFO() << "peer left: " << message.id.value;
            {
                std::scoped_lock lk(peers_mutex_);
                peers_.erase(message.id);
            }
            {
                std::scoped_lock lk(identity_mutex_);
                peer_usernames_.erase(message.id);
            }
            if (on_peer_left_) {
                on_peer_left_(message.id.value);
            }
        } else if constexpr (std::is_same_v<T, signaling::Answer>) {
            // Through the machine: whether an answer is meaningful depends on
            // the phase we are in, and it alone knows that.
            post_event(transport_fsm::AnswerReceived { message.sdp });
        } else if constexpr (std::is_same_v<T, signaling::Candidate>) {
            post_event(transport_fsm::RemoteCandidate {
                message.candidate, message.sdp_mid });
        } else if constexpr (std::is_same_v<T, signaling::StreamingStart>) {
            if (message.from && on_streaming_started_) {
                on_streaming_started_(message.from->value);
            }
        } else if constexpr (std::is_same_v<T, signaling::StreamingStop>) {
            if (message.from && on_streaming_stopped_) {
                on_streaming_stopped_(message.from->value);
            }
        } else if constexpr (std::is_same_v<T, signaling::WatchStart>) {
            if (message.from && on_watch_started_) {
                on_watch_started_(message.from->value);
            }
        } else if constexpr (std::is_same_v<T, signaling::WatchStop>) {
            if (message.from && on_watch_stopped_) {
                on_watch_stopped_(message.from->value);
            }
        } else if constexpr (std::is_same_v<T, signaling::Offer>) {
            LOG_WARNING() << "ignoring offer from signaling server";
        }
    },
        parsed.value());
}

void Transport::send_streaming_start()
{
    send_signal(signaling::encode(signaling::StreamingStart {}));
}

void Transport::send_streaming_stop()
{
    send_signal(signaling::encode(signaling::StreamingStop {}));
}

void Transport::send_watch_start()
{
    send_signal(signaling::encode(signaling::WatchStart {}));
}

void Transport::send_watch_stop()
{
    send_signal(signaling::encode(signaling::WatchStop {}));
}

void Transport::send_signal(const json& msg)
{
    std::scoped_lock lk(ws_mutex_);
    if (ws_ && ws_connected_) {
        ws_->send(msg.dump());
    }
}
