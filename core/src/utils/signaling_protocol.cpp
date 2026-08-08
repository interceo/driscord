#include "signaling_protocol.hpp"

#include <utility>

namespace signaling {
namespace {

bool required_string(const nlohmann::json& msg,
    const char* key,
    std::string& out)
{
    auto it = msg.find(key);
    if (it == msg.end() || !it->is_string()) {
        return false;
    }
    out = it->get<std::string>();
    return !out.empty();
}

std::optional<std::string> optional_string(const nlohmann::json& msg,
    const char* key)
{
    auto it = msg.find(key);
    if (it == msg.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        return std::string {};
    }
    return it->get<std::string>();
}

nlohmann::json with_type(std::string_view type)
{
    nlohmann::json msg = nlohmann::json::object();
    msg["type"] = std::string(type);
    return msg;
}

void add_from(nlohmann::json& msg, const std::optional<std::string>& from)
{
    if (from && !from->empty()) {
        msg["from"] = *from;
    }
}

} // namespace

std::string_view to_string(ParseError error)
{
    switch (error) {
    case ParseError::InvalidJson:
        return "invalid_json";
    case ParseError::MissingType:
        return "missing_type";
    case ParseError::UnknownType:
        return "unknown_type";
    case ParseError::MissingField:
        return "missing_field";
    case ParseError::InvalidField:
        return "invalid_field";
    }
    return "invalid_field";
}

nlohmann::json encode(const Welcome& value)
{
    auto msg = with_type("welcome");
    msg["id"] = value.id;

    auto peers = nlohmann::json::array();
    for (const auto& peer : value.peers) {
        peers.push_back({
            { "id", peer.id },
            { "username", peer.username },
        });
    }
    msg["peers"] = std::move(peers);
    msg["streaming_peers"] = value.streaming_peers;
    return msg;
}

nlohmann::json encode(const PeerJoined& value)
{
    auto msg = with_type("peer_joined");
    msg["id"] = value.id;
    msg["username"] = value.username;
    return msg;
}

nlohmann::json encode(const PeerLeft& value)
{
    auto msg = with_type("peer_left");
    msg["id"] = value.id;
    return msg;
}

nlohmann::json encode(const Offer& value)
{
    auto msg = with_type("offer");
    msg["sdp"] = value.sdp;
    return msg;
}

nlohmann::json encode(const Answer& value)
{
    auto msg = with_type("answer");
    msg["sdp"] = value.sdp;
    return msg;
}

nlohmann::json encode(const Candidate& value)
{
    auto msg = with_type("candidate");
    msg["candidate"] = value.candidate;
    msg["sdpMid"] = value.sdp_mid;
    return msg;
}

nlohmann::json encode(const StreamingStart& value)
{
    auto msg = with_type("streaming_start");
    add_from(msg, value.from);
    return msg;
}

nlohmann::json encode(const StreamingStop& value)
{
    auto msg = with_type("streaming_stop");
    add_from(msg, value.from);
    return msg;
}

nlohmann::json encode(const WatchStart& value)
{
    auto msg = with_type("watch_start");
    add_from(msg, value.from);
    return msg;
}

nlohmann::json encode(const WatchStop& value)
{
    auto msg = with_type("watch_stop");
    add_from(msg, value.from);
    return msg;
}

nlohmann::json encode(const Message& message)
{
    return std::visit([](const auto& value) { return encode(value); }, message);
}

utils::Expected<Message, ParseError> parse_json(const nlohmann::json& msg)
{
    if (!msg.is_object()) {
        return utils::Unexpected(ParseError::InvalidField);
    }

    std::string type;
    if (!required_string(msg, "type", type)) {
        return utils::Unexpected(ParseError::MissingType);
    }

    if (type == "welcome") {
        Welcome value;
        if (!required_string(msg, "id", value.id)) {
            return utils::Unexpected(ParseError::MissingField);
        }
        auto peers_it = msg.find("peers");
        if (peers_it != msg.end()) {
            if (!peers_it->is_array()) {
                return utils::Unexpected(ParseError::InvalidField);
            }
            for (const auto& item : *peers_it) {
                PeerIdentity peer;
                if (!item.is_object() || !required_string(item, "id", peer.id)) {
                    return utils::Unexpected(ParseError::InvalidField);
                }
                auto username = optional_string(item, "username");
                if (username && !username->empty()) {
                    peer.username = *username;
                }
                value.peers.push_back(std::move(peer));
            }
        }
        auto streaming_it = msg.find("streaming_peers");
        if (streaming_it != msg.end()) {
            if (!streaming_it->is_array()) {
                return utils::Unexpected(ParseError::InvalidField);
            }
            for (const auto& item : *streaming_it) {
                if (!item.is_string()) {
                    return utils::Unexpected(ParseError::InvalidField);
                }
                auto id = item.get<std::string>();
                if (!id.empty()) {
                    value.streaming_peers.push_back(std::move(id));
                }
            }
        }
        return Message { std::move(value) };
    }

    if (type == "peer_joined") {
        PeerJoined value;
        if (!required_string(msg, "id", value.id)) {
            return utils::Unexpected(ParseError::MissingField);
        }
        auto username = optional_string(msg, "username");
        if (username && !username->empty()) {
            value.username = *username;
        }
        return Message { std::move(value) };
    }

    if (type == "peer_left") {
        PeerLeft value;
        if (!required_string(msg, "id", value.id)) {
            return utils::Unexpected(ParseError::MissingField);
        }
        return Message { std::move(value) };
    }

    if (type == "offer") {
        Offer value;
        if (!required_string(msg, "sdp", value.sdp)) {
            return utils::Unexpected(ParseError::MissingField);
        }
        return Message { std::move(value) };
    }

    if (type == "answer") {
        Answer value;
        if (!required_string(msg, "sdp", value.sdp)) {
            return utils::Unexpected(ParseError::MissingField);
        }
        return Message { std::move(value) };
    }

    if (type == "candidate") {
        Candidate value;
        if (!required_string(msg, "candidate", value.candidate)
            || !required_string(msg, "sdpMid", value.sdp_mid)) {
            return utils::Unexpected(ParseError::MissingField);
        }
        return Message { std::move(value) };
    }

    if (type == "streaming_start") {
        return Message { StreamingStart { optional_string(msg, "from") } };
    }
    if (type == "streaming_stop") {
        return Message { StreamingStop { optional_string(msg, "from") } };
    }
    if (type == "watch_start") {
        return Message { WatchStart { optional_string(msg, "from") } };
    }
    if (type == "watch_stop") {
        return Message { WatchStop { optional_string(msg, "from") } };
    }

    return utils::Unexpected(ParseError::UnknownType);
}

utils::Expected<Message, ParseError> parse(std::string_view raw)
{
    try {
        return parse_json(nlohmann::json::parse(raw));
    } catch (const nlohmann::json::exception&) {
        return utils::Unexpected(ParseError::InvalidJson);
    }
}

std::string dump(const Message& message)
{
    return encode(message).dump();
}

} // namespace signaling
