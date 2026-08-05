#pragma once

// Connection lifecycle for one client's session on the server.
//
// Free of libdatachannel and of Boost.Beast: everything the machine does goes
// through the Actions interface, so the table can be exercised in a unit test
// with no socket and no PeerConnection.
//
// Threading: not thread-safe by design. Session serialises every event onto its
// own asio strand (see Session::post_fsm), so the machine only ever runs on one
// thread at a time. An sml::thread_safe policy would guard the machine's state
// but not the order in which its lock is taken relative to pc_mutex_, and an
// action blocking in PeerConnection::close() while an RTC worker waits for the
// same lock would deadlock.

#include <string>
#include <variant>

namespace session_fsm {

// --- events ------------------------------------------------------------------

struct OfferReceived {
    std::string sdp;
};
struct RemoteCandidate {
    std::string candidate;
    std::string mid;
};
struct ChannelOpened { };
struct PcFailed { };
struct SessionClosing { };

using Event = std::variant<OfferReceived,
    RemoteCandidate,
    ChannelOpened,
    PcFailed,
    SessionClosing>;

// --- side effects ------------------------------------------------------------

class Actions {
public:
    virtual ~Actions() = default;

    // Build a PeerConnection for this session and answer the offer.
    virtual void accept_offer(const std::string& sdp) = 0;
    virtual void apply_candidate(const std::string& candidate,
        const std::string& mid) = 0;
    // Idempotent teardown of the current PeerConnection and its channels.
    virtual void close_peer_connection() = 0;
    virtual void log_ignored(const char* what) = 0;
};

} // namespace session_fsm
