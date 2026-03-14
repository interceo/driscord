#include "voice_transport.hpp"

#include "log.hpp"

using json = nlohmann::json;

VoiceTransport::VoiceTransport() {
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun.l.google.com:19302"));
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun1.l.google.com:19302"));
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun2.l.google.com:19302"));
    rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun.cloudflare.com:3478"));
}

void VoiceTransport::add_turn_server(const std::string& url, const std::string& user, const std::string& pass) {
    // Parse "turn:host:port" or "turns:host:port"
    auto relay_type = rtc::IceServer::RelayType::TurnUdp;
    std::string host = url;

    if (host.rfind("turns:", 0) == 0) {
        host = host.substr(6);
        relay_type = rtc::IceServer::RelayType::TurnTls;
    } else if (host.rfind("turn:", 0) == 0) {
        host = host.substr(5);
    }

    uint16_t port = 3478;
    auto colon = host.rfind(':');
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

VoiceTransport::~VoiceTransport() { disconnect(); }

void VoiceTransport::connect(const std::string& ws_url) {
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

void VoiceTransport::disconnect() {
    // Move peers out first to avoid deadlock: close() triggers onClosed
    // callbacks that also lock peers_mutex_.
    std::unordered_map<std::string, PeerState> local_peers;
    {
        std::scoped_lock lk(peers_mutex_);
        local_peers = std::move(peers_);
        peers_.clear();
    }

    for (auto& [_, state] : local_peers) {
        if (state.dc) {
            state.dc->close();
        }
        if (state.video_dc) {
            state.video_dc->close();
        }
        if (state.pc) {
            state.pc->close();
        }
    }
    local_peers.clear();

    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::scoped_lock lk(ws_mutex_);
        ws = std::move(ws_);
        ws_connected_ = false;
    }
    if (ws) {
        ws->close();
    }
    local_id_.clear();
}

void VoiceTransport::send_audio(const uint8_t* data, size_t len) {
    std::scoped_lock lk(peers_mutex_);
    for (auto& [_, state] : peers_) {
        if (state.dc && state.dc_open) {
            try {
                state.dc->send(reinterpret_cast<const std::byte*>(data), len);
            } catch (const std::exception& e) {
                LOG_ERROR() << "send_audio: " << e.what();
            }
        }
    }
}

void VoiceTransport::send_video(const uint8_t* data, size_t len) {
    std::scoped_lock lk(peers_mutex_);
    for (auto& [_, state] : peers_) {
        if (state.video_dc && state.video_dc_open) {
            try {
                state.video_dc->send(reinterpret_cast<const std::byte*>(data), len);
            } catch (const std::exception& e) {
                LOG_ERROR() << "send_video: " << e.what();
            }
        }
    }
}

std::vector<VoiceTransport::PeerInfo> VoiceTransport::peers() const {
    std::scoped_lock lk(peers_mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (auto& [id, state] : peers_) {
        result.emplace_back(id, state.dc_open);
    }
    return result;
}

void VoiceTransport::on_ws_message(const std::string& raw) {
    try {
        auto msg = json::parse(raw);
        std::string type = msg.value("type", "");

        if (type == "welcome") {
            local_id_ = msg["id"];
            LOG_INFO() << "assigned id: " << local_id_;
            if (msg.contains("peers")) {
                for (auto& peer_id : msg["peers"]) {
                    std::string pid = peer_id;
                    LOG_INFO() << "existing peer: " << pid << ", creating offer";
                    create_peer(pid, true);
                }
            }
        } else if (type == "peer_joined") {
            std::string peer_id = msg["id"];
            LOG_INFO() << "peer joined: " << peer_id;
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
            // Close outside the lock to avoid deadlock
            if (removed.dc) {
                removed.dc->close();
            }
            if (removed.video_dc) {
                removed.video_dc->close();
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
        }
    } catch (const std::exception& e) {
        LOG_ERROR() << "on_ws_message: " << e.what();
    }
}

void VoiceTransport::create_peer(const std::string& peer_id, bool create_offer) {
    auto pc = std::make_shared<rtc::PeerConnection>(rtc_config_);

    pc->onLocalDescription([this, peer_id](rtc::Description desc) {
        json msg;
        msg["type"] = desc.typeString();
        msg["to"] = peer_id;
        msg["sdp"] = std::string(desc);
        send_signal(msg);
    });

    pc->onLocalCandidate([this, peer_id](rtc::Candidate cand) {
        LOG_INFO() << "ICE candidate for " << peer_id << ": " << std::string(cand);
        json msg;
        msg["type"] = "candidate";
        msg["to"] = peer_id;
        msg["candidate"] = std::string(cand);
        msg["sdpMid"] = cand.mid();
        send_signal(msg);
    });

    pc->onGatheringStateChange([peer_id](rtc::PeerConnection::GatheringState state) {
        LOG_INFO() << "peer " << peer_id << " ICE gathering: " << static_cast<int>(state);
    });

    pc->onDataChannel([this, peer_id](std::shared_ptr<rtc::DataChannel> dc) {
        std::string label = dc->label();
        if (label == "audio") {
            LOG_INFO() << "received audio channel from " << peer_id;
            setup_audio_channel(peer_id, dc);
        } else if (label == "video") {
            LOG_INFO() << "received video channel from " << peer_id;
            setup_video_channel(peer_id, dc);
        }
    });

    pc->onStateChange([peer_id](rtc::PeerConnection::State state) {
        LOG_INFO() << "peer " << peer_id << " state: " << state;
    });

    PeerState state;
    state.pc = pc;

    if (create_offer) {
        rtc::DataChannelInit audio_init;
        audio_init.reliability.unordered = true;
        audio_init.reliability.maxRetransmits = 0;

        auto audio_dc = pc->createDataChannel("audio", audio_init);
        setup_audio_channel(peer_id, audio_dc);
        state.dc = audio_dc;

        // Video must be reliable+ordered: a single lost H.264 packet
        // corrupts all subsequent P-frames until the next IDR keyframe.
        auto video_dc = pc->createDataChannel("video");
        setup_video_channel(peer_id, video_dc);
        state.video_dc = video_dc;
    }

    std::scoped_lock lk(peers_mutex_);
    peers_[peer_id] = std::move(state);
}

void VoiceTransport::handle_offer(const std::string& from, const std::string& sdp) {
    LOG_INFO() << "received offer from " << from;
    create_peer(from, false);

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
    }
}

void VoiceTransport::handle_answer(const std::string& from, const std::string& sdp) {
    LOG_INFO() << "received answer from " << from;

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
    }
}

void VoiceTransport::handle_candidate(const std::string& from, const std::string& candidate, const std::string& mid) {
    LOG_INFO() << "remote ICE candidate from " << from << ": " << candidate;
    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    }
}

void VoiceTransport::setup_audio_channel(const std::string& peer_id, std::shared_ptr<rtc::DataChannel> dc) {
    dc->onOpen([this, peer_id]() {
        LOG_INFO() << "audio channel open with " << peer_id;
        std::scoped_lock lk(peers_mutex_);
        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            it->second.dc_open = true;
        }
    });

    dc->onClosed([this, peer_id]() {
        LOG_INFO() << "audio channel closed with " << peer_id;
        std::scoped_lock lk(peers_mutex_);
        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            it->second.dc_open = false;
        }
    });

    dc->onMessage([this, peer_id](auto msg) {
        if (auto* data = std::get_if<rtc::binary>(&msg)) {
            if (on_audio_) {
                on_audio_(peer_id, reinterpret_cast<const uint8_t*>(data->data()), data->size());
            }
        }
    });

    dc->onError([peer_id](std::string error) { LOG_ERROR() << "audio dc error [" << peer_id << "]: " << error; });

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        it->second.dc = dc;
    }
}

void VoiceTransport::setup_video_channel(const std::string& peer_id, std::shared_ptr<rtc::DataChannel> dc) {
    dc->onOpen([this, peer_id]() {
        LOG_INFO() << "video channel open with " << peer_id;
        {
            std::scoped_lock lk(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second.video_dc_open = true;
            }
        }
        if (on_video_channel_opened_) {
            on_video_channel_opened_();
        }
    });

    dc->onClosed([this, peer_id]() {
        LOG_INFO() << "video channel closed with " << peer_id;
        std::scoped_lock lk(peers_mutex_);
        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            it->second.video_dc_open = false;
        }
    });

    dc->onMessage([this, peer_id](auto msg) {
        if (auto* data = std::get_if<rtc::binary>(&msg)) {
            if (on_video_) {
                on_video_(peer_id, reinterpret_cast<const uint8_t*>(data->data()), data->size());
            }
        }
    });

    dc->onError([peer_id](std::string error) { LOG_ERROR() << "video dc error [" << peer_id << "]: " << error; });

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        it->second.video_dc = dc;
    }
}

void VoiceTransport::send_signal(const json& msg) {
    std::scoped_lock lk(ws_mutex_);
    if (ws_ && ws_connected_) {
        ws_->send(msg.dump());
    }
}
