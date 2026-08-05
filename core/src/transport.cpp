#include "transport.hpp"

#include "log.hpp"
#include "transport_fsm_table.hpp"

#include <cstring>

using json = nlohmann::json;

namespace {
// Server → client media framing: u8 sender-id length, sender id, payload.
// Mirrors build_media_packet() in backend/signaling_server/src/ws_server.cpp.
constexpr size_t kSenderPrefixSize = 1;

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
    if (primary_channel_.empty()) {
        primary_channel_ = spec.label;
    }
    channel_specs_.push_back(std::move(spec));
}

utils::Expected<void, TransportError> Transport::connect(const std::string& ws_url)
{
    disconnect();
    ws_url_ = ws_url;

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
        json msg;
        msg["type"] = desc.typeString();
        msg["sdp"] = std::string(desc);
        send_signal(msg);
    });

    pc->onLocalCandidate([this](rtc::Candidate cand) {
        json msg;
        msg["type"] = "candidate";
        msg["candidate"] = std::string(cand);
        msg["sdpMid"] = cand.mid();
        send_signal(msg);
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
            auto dc = pc->createDataChannel(spec.label, init);
            setup_channel(spec.label, std::move(dc));
        } catch (const std::exception& e) {
            LOG_ERROR() << "createDataChannel[" << spec.label
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

void Transport::setup_channel(const std::string& label,
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

    if (!found) {
        LOG_WARNING() << "unknown channel label '" << label << "'";
        return;
    }

    dc->onOpen([this, label, on_open_cb]() {
        LOG_INFO() << "'" << label << "' channel open";
        {
            std::scoped_lock lk(pc_mutex_);
            channels_[label].open = true;
        }
        if (on_open_cb) {
            on_open_cb();
        }
    });

    dc->onClosed([this, label, on_close_cb]() {
        LOG_INFO() << "'" << label << "' channel closed";
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
    dc->onMessage([on_data, label](auto msg) {
        auto* data = std::get_if<rtc::binary>(&msg);
        if (!data || !on_data) {
            return;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(data->data());
        const size_t len = data->size();
        if (len < kSenderPrefixSize) {
            return;
        }
        const size_t from_len = bytes[0];
        if (len < kSenderPrefixSize + from_len) {
            LOG_WARNING() << "malformed media packet on '" << label << "' ("
                          << len << " bytes)";
            return;
        }
        std::string from(reinterpret_cast<const char*>(bytes + kSenderPrefixSize),
            from_len);
        on_data(from, bytes + kSenderPrefixSize + from_len,
            len - kSenderPrefixSize - from_len);
    });

    dc->onError([label](std::string error) {
        LOG_ERROR() << "'" << label << "' dc error: " << error;
    });

    std::scoped_lock lk(pc_mutex_);
    channels_[label].dc = std::move(dc);
}

std::shared_ptr<rtc::DataChannel> Transport::open_channel(
    const std::string& label) const
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
    std::unordered_map<std::string, ChannelState> channels;
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
    local_id_.clear();
}

void Transport::send_on_channel(const std::string& label,
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
        LOG_ERROR() << "send_on_channel[" << label << "]: " << e.what();
    }
}

void Transport::send_on_channel(const std::string& label, rtc::binary&& data)
{
    auto dc = open_channel(label);
    if (!dc) {
        return;
    }
    try {
        dc->send(std::move(data));
    } catch (const std::exception& e) {
        LOG_ERROR() << "send_on_channel[" << label << "]: " << e.what();
    }
}

size_t Transport::channel_buffered_amount(const std::string& label) const
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
        auto it = channels_.find(primary_channel_);
        media_up = it != channels_.end() && it->second.open;
    }

    std::scoped_lock lk(peers_mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto& id : peers_) {
        result.emplace_back(id, media_up);
    }
    return result;
}

std::string Transport::peer_username(const std::string& peer_id) const
{
    std::scoped_lock lk(identity_mutex_);
    auto it = peer_usernames_.find(peer_id);
    return it != peer_usernames_.end() ? it->second : "";
}

void Transport::set_peer_identity_(const std::string& peer_id, std::string username)
{
    if (username.empty()) {
        return;
    }
    std::function<void(const std::string&, const std::string&)> cb;
    {
        std::scoped_lock lk(identity_mutex_);
        peer_usernames_[peer_id] = username;
        cb = on_peer_identity_;
    }
    if (cb) {
        cb(peer_id, username);
    }
}

void Transport::on_ws_message(const std::string& raw)
{
    try {
        auto msg = json::parse(raw);
        std::string type = msg.value("type", "");

        if (type == "welcome") {
            std::string assigned_id = msg["id"];
            {
                std::scoped_lock lk(ws_mutex_);
                local_id_ = assigned_id;
            }
            LOG_INFO() << "assigned id: " << assigned_id;
            if (msg.contains("peers")) {
                for (auto& peer : msg["peers"]) {
                    std::string pid = peer.value("id", "");
                    if (pid.empty()) {
                        continue;
                    }
                    set_peer_identity_(pid, peer.value("username", ""));
                    {
                        std::scoped_lock lk(peers_mutex_);
                        peers_.insert(pid);
                    }
                    if (on_peer_joined_) {
                        on_peer_joined_(pid);
                    }
                }
            }
            if (msg.contains("streaming_peers")) {
                for (auto& sid : msg["streaming_peers"]) {
                    std::string id = sid;
                    if (on_streaming_started_) {
                        on_streaming_started_(id);
                    }
                }
            }
        } else if (type == "peer_joined") {
            std::string peer_id = msg["id"];
            LOG_INFO() << "peer joined: " << peer_id;
            set_peer_identity_(peer_id, msg.value("username", ""));
            {
                std::scoped_lock lk(peers_mutex_);
                peers_.insert(peer_id);
            }
            if (on_peer_joined_) {
                on_peer_joined_(peer_id);
            }
        } else if (type == "peer_left") {
            std::string peer_id = msg["id"];
            LOG_INFO() << "peer left: " << peer_id;
            {
                std::scoped_lock lk(peers_mutex_);
                peers_.erase(peer_id);
            }
            {
                std::scoped_lock lk(identity_mutex_);
                peer_usernames_.erase(peer_id);
            }
            if (on_peer_left_) {
                on_peer_left_(peer_id);
            }
        } else if (type == "answer") {
            // Through the machine: whether an answer is meaningful depends on
            // the phase we are in, and it alone knows that.
            post_event(transport_fsm::AnswerReceived { msg.value("sdp", "") });
        } else if (type == "candidate") {
            post_event(transport_fsm::RemoteCandidate {
                msg.value("candidate", ""), msg.value("sdpMid", "") });
        } else if (type == "streaming_start") {
            std::string from = msg["from"];
            if (on_streaming_started_) {
                on_streaming_started_(from);
            }
        } else if (type == "streaming_stop") {
            std::string from = msg["from"];
            if (on_streaming_stopped_) {
                on_streaming_stopped_(from);
            }
        } else if (type == "watch_start") {
            std::string from = msg["from"];
            if (on_watch_started_) {
                on_watch_started_(from);
            }
        } else if (type == "watch_stop") {
            std::string from = msg["from"];
            if (on_watch_stopped_) {
                on_watch_stopped_(from);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR() << "on_ws_message: " << e.what();
    }
}

void Transport::send_streaming_start()
{
    json msg;
    msg["type"] = "streaming_start";
    send_signal(msg);
}

void Transport::send_streaming_stop()
{
    json msg;
    msg["type"] = "streaming_stop";
    send_signal(msg);
}

void Transport::send_watch_start()
{
    json msg;
    msg["type"] = "watch_start";
    send_signal(msg);
}

void Transport::send_watch_stop()
{
    json msg;
    msg["type"] = "watch_stop";
    send_signal(msg);
}

void Transport::send_signal(const json& msg)
{
    std::scoped_lock lk(ws_mutex_);
    if (ws_ && ws_connected_) {
        ws_->send(msg.dump());
    }
}
