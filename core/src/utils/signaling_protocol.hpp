#pragma once

#include "expected.hpp"
#include "identity.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace signaling {

// Independent PeerConnections isolate latency-sensitive voice from
// bandwidth-heavy screen media.
enum class ConnectionId {
    Voice,
    Screen,
};

enum class WatchRejectReason {
    UnknownPeer,
    NotStreaming,
    Capacity,
};

enum class ParseError {
    InvalidJson,
    MissingType,
    UnknownType,
    MissingField,
    InvalidField,
};

struct PeerIdentity {
    driscord::PeerId id;
    driscord::Username username;
};

struct Welcome {
    driscord::PeerId id;
    std::vector<PeerIdentity> peers;
    std::vector<driscord::PeerId> streaming_peers;
};

struct PeerJoined {
    driscord::PeerId id;
    driscord::Username username;
};

struct PeerLeft {
    driscord::PeerId id;
};

struct Offer {
    std::string sdp;
    ConnectionId connection = ConnectionId::Voice;
};

struct Answer {
    std::string sdp;
    ConnectionId connection = ConnectionId::Voice;
};

struct Candidate {
    std::string candidate;
    std::string sdp_mid;
    ConnectionId connection = ConnectionId::Voice;
};

// Associates one pre-negotiated recvonly transceiver with the peer whose RTP
// the SFU currently forwards through it. A missing peer clears the slot.
struct TrackBinding {
    std::string sdp_mid;
    std::optional<driscord::PeerId> peer_id;
    ConnectionId connection = ConnectionId::Voice;
};

struct StreamingStart {
    std::optional<driscord::PeerId> from;
};

struct StreamingStop {
    std::optional<driscord::PeerId> from;
};

struct WatchStart {
    driscord::PeerId peer_id;
};

struct WatchStop {
    driscord::PeerId peer_id;
};

// Server response for a watch_start that could not be retained. Successful
// subscriptions are confirmed by their subsequent TrackBinding messages.
struct WatchRejected {
    driscord::PeerId peer_id;
    WatchRejectReason reason = WatchRejectReason::UnknownPeer;
};

using Message = std::variant<Welcome,
    PeerJoined,
    PeerLeft,
    Offer,
    Answer,
    Candidate,
    TrackBinding,
    StreamingStart,
    StreamingStop,
    WatchStart,
    WatchStop,
    WatchRejected>;

std::string_view to_string(ParseError error);
std::string_view to_string(ConnectionId connection);
std::string_view to_string(WatchRejectReason reason);

nlohmann::json encode(const Welcome& value);
nlohmann::json encode(const PeerJoined& value);
nlohmann::json encode(const PeerLeft& value);
nlohmann::json encode(const Offer& value);
nlohmann::json encode(const Answer& value);
nlohmann::json encode(const Candidate& value);
nlohmann::json encode(const TrackBinding& value);
nlohmann::json encode(const StreamingStart& value);
nlohmann::json encode(const StreamingStop& value);
nlohmann::json encode(const WatchStart& value);
nlohmann::json encode(const WatchStop& value);
nlohmann::json encode(const WatchRejected& value);
nlohmann::json encode(const Message& message);

utils::Expected<Message, ParseError> parse_json(const nlohmann::json& msg);
utils::Expected<Message, ParseError> parse(std::string_view raw);

std::string dump(const Message& message);

} // namespace signaling
