#include "media_connections.hpp"

#include "log.hpp"

#include <exception>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace driscord {

struct MediaConnections::Impl : std::enable_shared_from_this<Impl> {
    struct Connection {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::unordered_map<std::string, std::shared_ptr<rtc::Track>> tracks;
    };

    rtc::Configuration config;
    PeerId peer_id;
    SignalSender send_signal;
    TrackReady track_ready;
    std::mutex mutex;
    std::unordered_map<signaling::ConnectionId, Connection> connections;

    Impl(rtc::Configuration config_value,
        PeerId peer_id_value,
        SignalSender send_signal_value,
        TrackReady track_ready_value)
        : config(std::move(config_value))
        , peer_id(std::move(peer_id_value))
        , send_signal(std::move(send_signal_value))
        , track_ready(std::move(track_ready_value))
    {
    }

    void accept_offer(signaling::ConnectionId connection,
        const std::string& sdp)
    {
        // Client sessions preallocate their Unified Plan transceivers, so they
        // never renegotiate in place. A later offer for the same logical
        // connection comes from a newly-created Google PeerConnection and
        // must replace the old libdatachannel counterpart (including ICE).
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::scoped_lock lk(mutex);
            const auto it = connections.find(connection);
            if (it != connections.end()) {
                pc = it->second.pc;
            }
        }
        if (pc) {
            remove_and_close(connection, pc);
        }

        try {
            pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            LOG_ERROR() << "media peer connection create failed ["
                        << peer_id.value << ", "
                        << signaling::to_string(connection) << "]: "
                        << e.what();
            return;
        }

        const std::weak_ptr<Impl> weak = weak_from_this();
        const std::weak_ptr<rtc::PeerConnection> weak_pc = pc;
        pc->onLocalDescription(
            [weak, weak_pc, connection](rtc::Description description) {
                auto self = weak.lock();
                auto owner = weak_pc.lock();
                if (!self || !owner || !self->owns(connection, owner)) {
                    return;
                }
                if (description.typeString() == "answer") {
                    self->send_signal(signaling::dump(signaling::Answer {
                        std::string(description), connection }));
                }
            });
        pc->onLocalCandidate(
            [weak, weak_pc, connection](rtc::Candidate candidate) {
                auto self = weak.lock();
                auto owner = weak_pc.lock();
                if (!self || !owner || !self->owns(connection, owner)) {
                    return;
                }
                self->send_signal(signaling::dump(signaling::Candidate {
                    std::string(candidate), candidate.mid(), connection }));
            });
        pc->onTrack([weak, weak_pc, connection](std::shared_ptr<rtc::Track> track) {
            auto self = weak.lock();
            auto owner = weak_pc.lock();
            if (!self || !owner || !track) {
                return;
            }
            self->register_track(connection, owner, std::move(track));
        });
        pc->onStateChange([weak, connection](rtc::PeerConnection::State state) {
            if (auto self = weak.lock()) {
                LOG_INFO() << "session " << self->peer_id.value << " "
                           << signaling::to_string(connection) << " pc state: "
                           << static_cast<int>(state);
            }
        });

        {
            std::scoped_lock lk(mutex);
            auto& slot = connections[connection];
            slot.tracks.clear();
            slot.pc = pc;
        }
        apply_offer(pc, connection, sdp, "media setRemoteDescription failed");
    }

    void add_remote_candidate(signaling::ConnectionId connection,
        const std::string& candidate,
        const std::string& mid)
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::scoped_lock lk(mutex);
            const auto it = connections.find(connection);
            if (it != connections.end()) {
                pc = it->second.pc;
            }
        }
        if (!pc) {
            LOG_WARNING() << "candidate with no "
                          << signaling::to_string(connection)
                          << " peer connection [" << peer_id.value
                          << "], dropping";
            return;
        }
        try {
            pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
        } catch (const std::exception& e) {
            LOG_ERROR() << "media addRemoteCandidate failed [" << peer_id.value
                        << ", " << signaling::to_string(connection)
                        << "]: " << e.what();
        }
    }

    bool owns(signaling::ConnectionId connection,
        const std::shared_ptr<rtc::PeerConnection>& pc)
    {
        std::scoped_lock lk(mutex);
        const auto it = connections.find(connection);
        return it != connections.end() && it->second.pc == pc;
    }

    void register_track(signaling::ConnectionId connection,
        const std::shared_ptr<rtc::PeerConnection>& pc,
        std::shared_ptr<rtc::Track> track)
    {
        const std::string mid = track->mid();
        std::shared_ptr<rtc::Track> replaced;
        {
            std::scoped_lock lk(mutex);
            auto it = connections.find(connection);
            if (it == connections.end() || it->second.pc != pc) {
                return;
            }
            const auto existing = it->second.tracks.find(mid);
            if (existing != it->second.tracks.end()) {
                replaced = std::move(existing->second);
            }
            it->second.tracks[mid] = track;
        }
        // Track callbacks may re-enter this object, so never close or destroy
        // the last strong reference while holding `mutex`.
        if (replaced && replaced != track) {
            replaced->close();
        }

        const std::weak_ptr<Impl> weak = weak_from_this();
        const std::weak_ptr<rtc::Track> weak_track = track;
        track->onClosed([weak, weak_track, connection, mid] {
            auto self = weak.lock();
            auto owner = weak_track.lock();
            if (!self || !owner) {
                return;
            }
            std::scoped_lock lk(self->mutex);
            auto connection_it = self->connections.find(connection);
            if (connection_it == self->connections.end()) {
                return;
            }
            auto track_it = connection_it->second.tracks.find(mid);
            if (track_it != connection_it->second.tracks.end()
                && track_it->second == owner) {
                connection_it->second.tracks.erase(track_it);
            }
        });
        LOG_INFO() << "session " << peer_id.value << " registered "
                   << signaling::to_string(connection) << " track mid=" << mid;
        if (track_ready) {
            try {
                track_ready(connection, std::move(track));
            } catch (const std::exception& error) {
                LOG_ERROR() << "media track registration callback failed ["
                            << peer_id.value << ", "
                            << signaling::to_string(connection) << "]: "
                            << error.what();
            }
        }
    }

    void apply_offer(const std::shared_ptr<rtc::PeerConnection>& pc,
        signaling::ConnectionId connection,
        const std::string& sdp,
        const char* error_context)
    {
        try {
            pc->setRemoteDescription(
                rtc::Description(sdp, rtc::Description::Type::Offer));
        } catch (const std::exception& e) {
            LOG_ERROR() << error_context << " [" << peer_id.value << ", "
                        << signaling::to_string(connection) << "]: " << e.what();
            remove_and_close(connection, pc);
        }
    }

    void remove_and_close(signaling::ConnectionId connection,
        const std::shared_ptr<rtc::PeerConnection>& pc)
    {
        Connection removed;
        bool found = false;
        {
            std::scoped_lock lk(mutex);
            const auto it = connections.find(connection);
            if (it != connections.end() && it->second.pc == pc) {
                removed = std::move(it->second);
                connections.erase(it);
                found = true;
            }
        }
        if (!found) {
            return;
        }
        for (auto& [_, track] : removed.tracks) {
            if (track) {
                track->close();
            }
        }
        if (removed.pc) {
            removed.pc->close();
        }
    }

    void close()
    {
        std::unordered_map<signaling::ConnectionId, Connection> owned;
        {
            std::scoped_lock lk(mutex);
            owned = std::move(connections);
            connections.clear();
        }
        for (auto& [_, connection] : owned) {
            for (auto& [__, track] : connection.tracks) {
                if (track) {
                    track->close();
                }
            }
            if (connection.pc) {
                connection.pc->close();
            }
        }
    }
};

MediaConnections::MediaConnections(rtc::Configuration config,
    PeerId peer_id,
    SignalSender send_signal,
    TrackReady track_ready)
    : impl_(std::make_shared<Impl>(
          std::move(config), std::move(peer_id), std::move(send_signal),
          std::move(track_ready)))
{
}

MediaConnections::~MediaConnections()
{
    close();
}

void MediaConnections::accept_offer(signaling::ConnectionId connection,
    const std::string& sdp)
{
    impl_->accept_offer(connection, sdp);
}

void MediaConnections::add_remote_candidate(
    signaling::ConnectionId connection,
    const std::string& candidate,
    const std::string& mid)
{
    impl_->add_remote_candidate(connection, candidate, mid);
}

void MediaConnections::close()
{
    if (impl_) {
        impl_->close();
    }
}

} // namespace driscord
