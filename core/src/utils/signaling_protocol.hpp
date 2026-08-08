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
};

struct Answer {
    std::string sdp;
};

struct Candidate {
    std::string candidate;
    std::string sdp_mid;
};

struct StreamingStart {
    std::optional<driscord::PeerId> from;
};

struct StreamingStop {
    std::optional<driscord::PeerId> from;
};

struct WatchStart {
    std::optional<driscord::PeerId> from;
};

struct WatchStop {
    std::optional<driscord::PeerId> from;
};

using Message = std::variant<Welcome,
    PeerJoined,
    PeerLeft,
    Offer,
    Answer,
    Candidate,
    StreamingStart,
    StreamingStop,
    WatchStart,
    WatchStop>;

std::string_view to_string(ParseError error);

nlohmann::json encode(const Welcome& value);
nlohmann::json encode(const PeerJoined& value);
nlohmann::json encode(const PeerLeft& value);
nlohmann::json encode(const Offer& value);
nlohmann::json encode(const Answer& value);
nlohmann::json encode(const Candidate& value);
nlohmann::json encode(const StreamingStart& value);
nlohmann::json encode(const StreamingStop& value);
nlohmann::json encode(const WatchStart& value);
nlohmann::json encode(const WatchStop& value);
nlohmann::json encode(const Message& message);

utils::Expected<Message, ParseError> parse_json(const nlohmann::json& msg);
utils::Expected<Message, ParseError> parse(std::string_view raw);

std::string dump(const Message& message);

} // namespace signaling
