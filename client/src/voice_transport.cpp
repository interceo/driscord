#include "voice_transport.hpp"

#include <iostream>

using json = nlohmann::json;


VoiceTransport::VoiceTransport() { rtc_config_.iceServers.push_back(rtc::IceServer("stun:stun.l.google.com:19302")); }

VoiceTransport::~VoiceTransport() { disconnect(); }

void VoiceTransport::connect(const std::string& ws_url) {
    disconnect();
    ws_url_ = ws_url;

    ws_ = std::make_shared<rtc::WebSocket>();

    ws_->onOpen([this]() {
        std::cout << "ws connected to " << ws_url_ << std::endl;
        ws_connected_ = true;
    });

    ws_->onClosed([this]() {
        std::cout << "ws disconnected" << std::endl;
        ws_connected_ = false;
    });

    ws_->onError([](std::string error) { std::cerr << "ws error: " << error << std::endl; });

    ws_->onMessage([this](auto msg) {
        if (auto* str = std::get_if<std::string>(&msg)) {
            on_ws_message(*str);
        }
    });

    ws_->open(ws_url);
}

void VoiceTransport::disconnect() {
    {
        std::scoped_lock lk(peers_mutex_);
        for (auto& [_, state] : peers_) {
            if (state.dc) {
                state.dc->close();
            }
            if (state.pc) {
                state.pc->close();
            }
        }
        peers_.clear();
    }

    if (ws_) {
        ws_->close();
        ws_.reset();
    }
    ws_connected_ = false;
    local_id_.clear();
}

void VoiceTransport::send_audio(const uint8_t* data, size_t len) {
    std::scoped_lock lk(peers_mutex_);
    for (auto& [_, state] : peers_) {
        if (state.dc && state.dc_open) {
            try {
                state.dc->send(reinterpret_cast<const std::byte*>(data), len);
            } catch (const std::exception& e) {
                std::cerr << "send_audio error: " << e.what() << std::endl;
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
            std::cout << "assigned id: " << local_id_ << std::endl;

            if (msg.contains("peers")) {
                for (auto& peer_id : msg["peers"]) {
                    std::string pid = peer_id;
                    std::cout << "existing peer: " << pid << ", creating offer" << std::endl;
                    create_peer(pid, true);
                }
            }
        } else if (type == "peer_joined") {
            std::string peer_id = msg["id"];
            std::cout << "peer joined: " << peer_id << std::endl;
            if (on_peer_joined_) {
                on_peer_joined_(peer_id);
            }
        } else if (type == "peer_left") {
            std::string peer_id = msg["id"];
            std::cout << "peer left: " << peer_id << std::endl;
            {
                std::scoped_lock lk(peers_mutex_);
                peers_.erase(peer_id);
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
        std::cerr << "on_ws_message error: " << e.what() << std::endl;
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
        json msg;
        msg["type"] = "candidate";
        msg["to"] = peer_id;
        msg["candidate"] = std::string(cand);
        msg["sdpMid"] = cand.mid();
        send_signal(msg);
    });

    pc->onDataChannel([this, peer_id](std::shared_ptr<rtc::DataChannel> dc) {
        std::cout << "received data channel from " << peer_id << std::endl;
        setup_data_channel(peer_id, dc);
    });

    pc->onStateChange([peer_id](rtc::PeerConnection::State state) {
        std::cout << "peer " << peer_id << " state: " << state << std::endl;
    });

    PeerState state;
    state.pc = pc;

    if (create_offer) {
        rtc::DataChannelInit init;
        init.reliability.unordered = true;
        init.reliability.maxRetransmits = 0;
        auto dc = pc->createDataChannel("audio", init);
        setup_data_channel(peer_id, dc);
        state.dc = dc;
    }

    std::scoped_lock lk(peers_mutex_);
    peers_[peer_id] = std::move(state);
}

void VoiceTransport::handle_offer(const std::string& from, const std::string& sdp) {
    std::cout << "received offer from " << from << std::endl;
    create_peer(from, false);

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
    }
}

void VoiceTransport::handle_answer(const std::string& from, const std::string& sdp) {
    std::cout << "received answer from " << from << std::endl;
    
    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
    }
}

void VoiceTransport::handle_candidate(const std::string& from, const std::string& candidate, const std::string& mid) {
    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(from);
    if (it != peers_.end()) {
        it->second.pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    }
}

void VoiceTransport::setup_data_channel(const std::string& peer_id, std::shared_ptr<rtc::DataChannel> dc) {
    dc->onOpen([this, peer_id]() {
        std::cout << "data channel open with " << peer_id << std::endl;
        std::scoped_lock lk(peers_mutex_);
        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            it->second.dc_open = true;
        }
    });

    dc->onClosed([this, peer_id]() {
        std::cout << "data channel closed with " << peer_id << std::endl;
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

    dc->onError([peer_id](std::string error) { std::cerr << "dc error [" << peer_id << "]: " << error << std::endl; });

    std::scoped_lock lk(peers_mutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        it->second.dc = dc;
    }
}

void VoiceTransport::send_signal(const json& msg) {
    if (ws_ && ws_connected_) {
        ws_->send(msg.dump());
    }
}
