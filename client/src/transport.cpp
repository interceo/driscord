#include "transport.hpp"

#include "log.hpp"

using json = nlohmann::json;

Transport::Transport() {
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun.l.google.com:19302"));
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun1.l.google.com:19302"));
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun2.l.google.com:19302"));
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun.cloudflare.com:3478"));
    // Each video frame is chunked to 60 KB at the application level (VideoTransport),
    // so SCTP never sees messages larger than ~61 KB. 128 KB gives enough headroom.
    rtc_config_.maxMessageSize = 128 * 1024; // 128 KB
}

Transport::~Transport() {
    disconnect();
}

void Transport::register_channel(ChannelSpec spec) {
    if (primary_channel_.empty()) {
        primary_channel_ = spec.label;
    }
    channel_specs_.push_back(std::move(spec));
}

void Transport::add_turn_server(const std::string& url, const std::string& user, const std::string& pass) {
    auto relay_type  = rtc::IceServer::RelayType::TurnUdp;
    std::string host = url;

    if (host.rfind("turns:", 0) == 0) {
        host       = host.substr(6);
        relay_type = rtc::IceServer::RelayType::TurnTls;
    } else if (host.rfind("turn:", 0) == 0) {
        host = host.substr(5);
    }

    uint16_t port = 3478;
    auto colon    = host.rfind(':');
    if (colon != std::string::npos) {
        try {
            port = static_cast<uint16_t>(std::stoi(host.substr(colon + 1)));
        } catch (const std::exception&) {
            LOG_ERROR() << "invalid TURN port in URL: " << url;
            return;
        }
        host = host.substr(0, colon);
    }

    rtc_config_.iceServers.push_back(rtc::IceServer(host, port, user, pass, relay_type));
    LOG_INFO() << "TURN server added: " << host << ":" << port;
}

void Transport::connect(const std::string& ws_url) {
    disconnect();
    ws_url_ = ws_url;

    auto ws = std::make_shared<rtc::WebSocket>();

    ws->onOpen([this]() {
        LOG_INFO() << "ws connected to " << ws_url_;
        ws_connected_ = true;
    });

    ws->onClosed([this]() {
        LOG_INFO() << "ws disconnected";
        ws_connected_ = false;
    });

    ws->onError([](std::string error) { LOG_ERROR() << "ws error: " << error; });

    ws->onMessage([this](auto msg) {
        if (auto* str = std::get_if<std::string>(&msg)) {
            on_ws_message(*str);
        }
    });

    ws->open(ws_url);

    std::scoped_lock lk(ws_mutex_);
    ws_ = std::move(ws);
}

void Transport::disconnect() {
    std::unordered_map<std::string, PeerState> local_peers;
    {
        std::scoped_lock lk(peers_mutex_);
        local_peers = std::move(peers_);
        peers_.clear();
    }

    for (auto& [_, state] : local_peers) {
        for (auto& [label, ch] : state.channels) {
            if (ch.dc) {
                ch.dc->close();
            }
        }
        if (state.pc) {
            state.pc->close();
        }
    }
    local_peers.clear();

    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::scoped_lock lk(ws_mutex_);
        ws            = std::move(ws_);
        ws_connected_ = false;
    }
    if (ws) {
        ws->close();
    }
    local_id_.clear();
}

void Transport::send_on_channel(const std::string& label, const uint8_t* data, size_t len) {
    std::vector<std::shared_ptr<rtc::DataChannel>> targets;
    {
        std::scoped_lock lk(peers_mutex_);
        for (auto& [_, state] : peers_) {
            auto it = state.channels.find(label);
            if (it != state.channels.end() && it->second.dc && it->second.open) {
                targets.push_back(it->second.dc);
            }
        }
    }
    for (const auto& dc : targets) {
        try {
            dc->send(reinterpret_cast<const std::byte*>(data), len);
        } catch (const std::exception& e) {
            LOG_ERROR() << "send_on_channel[" << label << "]: " << e.what();
        }
    }
}

void Transport::send_on_channel_to(
    const std::string& label,
    const std::string& peer_id,
    const uint8_t* data,
    size_t len
) {
    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::scoped_lock lk(peers_mutex_);
        auto pit = peers_.find(peer_id);
        if (pit == peers_.end()) return;
        auto cit = pit->second.channels.find(label);
        if (cit == pit->second.channels.end() || !cit->second.dc || !cit->second.open) return;
        dc = cit->second.dc;
    }
    try {
        dc->send(reinterpret_cast<const std::byte*>(data), len);
    } catch (const std::exception& e) {
        LOG_ERROR() << "send_on_channel_to[" << label << "][" << peer_id << "]: " << e.what();
    }
}

std::vector<Transport::PeerInfo> Transport::peers() const {
    std::scoped_lock lk(peers_mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (auto& [id, state] : peers_) {
        bool primary_open = false;
        if (auto it = state.channels.find(primary_channel_); it != state.channels.end()) {
            primary_open = it->second.open;
        }
        result.emplace_back(id, primary_open);
    }
    return result;
}

void Transport::on_ws_message(const std::string& raw) {
    try {
        auto msg         = json::parse(raw);
        std::string type = msg.value("type", "");

        if (type == "welcome") {
            std::string assigned_id = msg["id"];
            {
                std::scoped_lock lk(ws_mutex_);
                local_id_ = assigned_id;
            }
            LOG_INFO() << "assigned id: " << assigned_id;
            if (msg.contains("peers")) {
                for (auto& peer_id : msg["peers"]) {
                    std::string pid = peer_id;
                    LOG_INFO() << "existing peer: " << pid << ", creating offer";
                    create_peer(pid, true);
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
            {
                // Pre-register peer so peers() returns it before offer arrives
                std::scoped_lock lk(peers_mutex_);
                peers_.emplace(peer_id, PeerState{});
            }
            if (on_peer_joined_) {
                on_peer_joined_(peer_id);
            }
        } else if (type == "peer_left") {
            std::string peer_id = msg["id"];
            LOG_INFO() << "peer left: " << peer_id;

            PeerState removed;
            {
                std::scoped_lock lk(peers_mutex_);
                auto it = peers_.find(peer_id);
                if (it != peers_.end()) {
                    removed = std::move(it->second);
                    peers_.erase(it);
                }
            }
            for (auto& [label, ch] : removed.channels) {
                if (ch.dc) {
                    ch.dc->close();
                }
            }
            if (removed.pc) {
                removed.pc->close();
            }
            if (on_peer_left_) {
                on_peer_left_(peer_id);
            }
        } else if (type == "offer") {
            handle_offer(msg["from"], msg["sdp"]);
        } else if (type == "answer") {
            handle_answer(msg["from"], msg["sdp"]);
        } else if (type == "candidate") {
            handle_candidate(msg["from"], msg["candidate"], msg.value("sdpMid", ""));
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

void Transport::create_peer(const std::string& peer_id, bool create_offer) {
    auto pc = std::make_shared<rtc::PeerConnection>(rtc_config_);

    pc->onLocalDescription([this, peer_id](rtc::Description desc) {
        json msg;
        msg["type"] = desc.typeString();
        msg["to"]   = peer_id;
        msg["sdp"]  = std::string(desc);
        send_signal(msg);
    });

    pc->onLocalCandidate([this, peer_id](rtc::Candidate cand) {
        LOG_INFO() << "ICE candidate for " << peer_id << ": " << std::string(cand);
        json msg;
        msg["type"]      = "candidate";
        msg["to"]        = peer_id;
        msg["candidate"] = std::string(cand);
        msg["sdpMid"]    = cand.mid();
        send_signal(msg);
    });

    pc->onGatheringStateChange([peer_id](rtc::PeerConnection::GatheringState state) {
        LOG_INFO() << "peer " << peer_id << " ICE gathering: " << static_cast<int>(state);
    });

    pc->onDataChannel([this, peer_id](std::shared_ptr<rtc::DataChannel> dc) {
        const std::string label = dc->label();
        LOG_INFO() << "received channel '" << label << "' from " << peer_id;
        setup_channel(peer_id, label, dc);
    });

    pc->onStateChange([peer_id](rtc::PeerConnection::State state) {
        LOG_INFO() << "peer " << peer_id << " state: " << state;
    });

    PeerState state;
    state.pc = pc;

    if (create_offer) {
        for (const auto& spec : channel_specs_) {
            rtc::DataChannelInit init;
            init.reliability.unordered = spec.unordered;
            if (spec.max_retransmits >= 0) {
                init.reliability.maxRetransmits = spec.max_retransmits;
            }
            auto dc                       = pc->createDataChannel(spec.label, init);
            state.channels[spec.label].dc = dc;
            setup_channel(peer_id, spec.label, dc);
        }
    }

    std::scoped_lock lk(peers_mutex_);
    peers_[peer_id] = std::move(state);
}

void Transport::handle_offer(const std::string& from, const std::string& sdp) {
    LOG_INFO() << "received offer from " << from;
    create_peer(from, false);

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
    }
}

void Transport::handle_answer(const std::string& from, const std::string& sdp) {
    LOG_INFO() << "received answer from " << from;

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
    }
}

void Transport::handle_candidate(const std::string& from, const std::string& candidate, const std::string& mid) {
    LOG_INFO() << "remote ICE candidate from " << from << ": " << candidate;
    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    }
}

void Transport::setup_channel(
    const std::string& peer_id,
    const std::string& label,
    std::shared_ptr<rtc::DataChannel> dc
) {
    // Find the registered spec for this label.
    PacketCb on_data;
    PeerEventCb on_open_cb;
    PeerEventCb on_close_cb;
    bool found = false;

    for (const auto& spec : channel_specs_) {
        if (spec.label == label) {
            on_data     = spec.on_data;
            on_open_cb  = spec.on_open;
            on_close_cb = spec.on_close;
            found       = true;
            break;
        }
    }

    if (!found) {
        LOG_WARNING() << "unknown channel label '" << label << "' from " << peer_id;
        return;
    }

    dc->onOpen([this, peer_id, label, on_open_cb]() {
        LOG_INFO() << "'" << label << "' channel open with " << peer_id;
        {
            std::scoped_lock lk(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second.channels[label].open = true;
            }
        }
        if (on_open_cb) {
            on_open_cb(peer_id);
        }
    });

    dc->onClosed([this, peer_id, label, on_close_cb]() {
        LOG_INFO() << "'" << label << "' channel closed with " << peer_id;
        {
            std::scoped_lock lk(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second.channels[label].open = false;
            }
        }
        if (on_close_cb) {
            on_close_cb(peer_id);
        }
    });

    dc->onMessage([on_data, peer_id](auto msg) {
        if (auto* data = std::get_if<rtc::binary>(&msg)) {
            if (on_data) {
                on_data(peer_id, reinterpret_cast<const uint8_t*>(data->data()), data->size());
            }
        }
    });

    dc->onError([peer_id, label](std::string error) {
        LOG_ERROR() << "'" << label << "' dc error [" << peer_id << "]: " << error;
    });

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        it->second.channels[label].dc = dc;
    }
}

void Transport::send_streaming_start() {
    json msg;
    msg["type"] = "streaming_start";
    send_signal(msg);
}

void Transport::send_streaming_stop() {
    json msg;
    msg["type"] = "streaming_stop";
    send_signal(msg);
}

void Transport::send_watch_start() {
    json msg;
    msg["type"] = "watch_start";
    send_signal(msg);
}

void Transport::send_watch_stop() {
    json msg;
    msg["type"] = "watch_stop";
    send_signal(msg);
}

void Transport::send_signal(const json& msg) {
    std::scoped_lock lk(ws_mutex_);
    if (ws_ && ws_connected_) {
        ws_->send(msg.dump());
    }
}
