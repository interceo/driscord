#pragma once

// Connection lifecycle for the client's single media link to the server.
//
// Deliberately free of libdatachannel: everything the machine does to the
// outside world goes through the Actions interface below, so the whole table
// can be exercised in a unit test without a network or a PeerConnection.
//
// The transition table itself lives in transport_fsm_table.hpp, which is the
// only place <boost/sml.hpp> is pulled in — this header stays light enough to
// be included from transport.hpp.
//
// Threading: this machine is NOT thread-safe and is not meant to be. Transport
// serialises every event onto one thread (see Transport::fsm_loop_), which is
// why no sml::thread_safe policy is used — that policy guards the machine's own
// state but not the order in which its lock is taken relative to pc_mutex_,
// and an action that blocks in PeerConnection::close() while an RTC worker
// waits for the same lock would deadlock.

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

namespace transport_fsm {

// --- events ------------------------------------------------------------------

struct ConnectRequested { };
struct WsOpened { };
struct WsClosed { };
struct AnswerReceived {
    std::string sdp;
};
struct RemoteCandidate {
    std::string candidate;
    std::string mid;
};
struct PcConnected { };
struct PcFailed {
    int64_t now_ms = 0;
};
struct DisconnectRequested { };
// Periodic wake-up from the driving loop; carries the clock so the backoff
// guard stays a pure function of its input and tests can supply fake time.
struct Tick {
    int64_t now_ms = 0;
};

// Everything the queue can carry, so events can be posted from any thread and
// replayed on the FSM thread.
using Event = std::variant<ConnectRequested,
    WsOpened,
    WsClosed,
    AnswerReceived,
    RemoteCandidate,
    PcConnected,
    PcFailed,
    DisconnectRequested,
    Tick>;

// --- side effects ------------------------------------------------------------

class Actions {
public:
    virtual ~Actions() = default;

    // Build the PeerConnection and its channels, then send the offer. Also the
    // renegotiation path: called again when reconnecting.
    virtual void create_peer_connection() = 0;
    virtual void apply_answer(const std::string& sdp) = 0;
    virtual void apply_candidate(const std::string& candidate,
        const std::string& mid) = 0;
    virtual void close_peer_connection() = 0;
    // An event that carried real information was dropped because it made no
    // sense in the current state. Worth a log line — silently swallowing
    // signaling is painful to debug.
    virtual void log_ignored(const char* what) = 0;
};

// --- reconnect backoff -------------------------------------------------------

struct Backoff {
    static constexpr int64_t kFirstDelayMs = 1000;
    static constexpr int64_t kMaxDelayMs = 10000;

    int64_t delay_ms = 0;
    int64_t due_at_ms = 0;

    void arm(int64_t now_ms)
    {
        delay_ms = delay_ms ? std::min(delay_ms * 2, kMaxDelayMs) : kFirstDelayMs;
        due_at_ms = now_ms + delay_ms;
    }
    void reset()
    {
        delay_ms = 0;
        due_at_ms = 0;
    }
    bool elapsed(int64_t now_ms) const { return now_ms >= due_at_ms; }
};

} // namespace transport_fsm
