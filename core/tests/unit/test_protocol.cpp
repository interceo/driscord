#include "utils/signaling_protocol.hpp"

#include <gtest/gtest.h>

#include <variant>

TEST(SignalingProtocol, WelcomeRoundtrip)
{
    signaling::Welcome source;
    source.id = driscord::PeerId { "self" };
    source.peers.push_back(
        { driscord::PeerId { "peer-1" }, driscord::Username { "alice" } });
    source.streaming_peers.push_back(driscord::PeerId { "peer-2" });

    const auto parsed = signaling::parse(signaling::dump(source));
    ASSERT_TRUE(parsed);
    const auto* welcome = std::get_if<signaling::Welcome>(&parsed.value());
    ASSERT_NE(welcome, nullptr);
    EXPECT_EQ(welcome->id.value, "self");
    ASSERT_EQ(welcome->peers.size(), 1u);
    EXPECT_EQ(welcome->peers.front().id.value, "peer-1");
    EXPECT_EQ(welcome->peers.front().username.value, "alice");
    ASSERT_EQ(welcome->streaming_peers.size(), 1u);
    EXPECT_EQ(welcome->streaming_peers.front().value, "peer-2");
}

TEST(SignalingProtocol, ConnectionIdsAreMandatoryAndRoundtrip)
{
    const auto offer_json = signaling::encode(
        signaling::Offer { "v=0", signaling::ConnectionId::Voice });
    EXPECT_EQ(offer_json.at("connection"), "voice");

    const auto offer = signaling::parse(offer_json.dump());
    ASSERT_TRUE(offer);
    EXPECT_EQ(std::get<signaling::Offer>(offer.value()).connection,
        signaling::ConnectionId::Voice);

    const auto candidate = signaling::parse(signaling::dump(
        signaling::Candidate { "candidate:1", "0",
            signaling::ConnectionId::Screen }));
    ASSERT_TRUE(candidate);
    EXPECT_EQ(std::get<signaling::Candidate>(candidate.value()).connection,
        signaling::ConnectionId::Screen);

    for (const auto* raw : {
             R"({"type":"offer","sdp":"v=0"})",
             R"({"type":"candidate","candidate":"x","sdpMid":"0"})",
             R"({"type":"answer","sdp":"v=0","connection":"legacy"})",
         }) {
        EXPECT_FALSE(signaling::parse(raw)) << raw;
    }
}

TEST(SignalingProtocol, TrackBindingRoundtripAndClear)
{
    const auto bound = signaling::parse(signaling::dump(
        signaling::TrackBinding { "3", driscord::PeerId { "speaker" },
            signaling::ConnectionId::Voice }));
    ASSERT_TRUE(bound);
    const auto& binding = std::get<signaling::TrackBinding>(bound.value());
    ASSERT_TRUE(binding.peer_id);
    EXPECT_EQ(binding.sdp_mid, "3");
    EXPECT_EQ(binding.peer_id->value, "speaker");
    EXPECT_EQ(binding.connection, signaling::ConnectionId::Voice);

    const auto cleared = signaling::parse(signaling::dump(
        signaling::TrackBinding { "3", std::nullopt,
            signaling::ConnectionId::Voice }));
    ASSERT_TRUE(cleared);
    EXPECT_FALSE(std::get<signaling::TrackBinding>(cleared.value()).peer_id);
}

TEST(SignalingProtocol, RejectsMalformedFields)
{
    for (const auto* raw : {
             R"({"type":"offer","sdp":"v=0","connection":"other"})",
             R"({"type":"offer","sdp":"v=0","connection":null})",
             R"({"type":"track_binding","sdpMid":"","peerId":null,"connection":"voice"})",
             R"({"type":"track_binding","sdpMid":"1","peerId":7,"connection":"voice"})",
         }) {
        EXPECT_FALSE(signaling::parse(raw)) << raw;
    }
}

TEST(SignalingProtocol, ControlMessageCanCarrySender)
{
    const auto parsed = signaling::parse(signaling::dump(
        signaling::StreamingStart { driscord::PeerId { "peer-1" } }));
    ASSERT_TRUE(parsed);
    const auto& start = std::get<signaling::StreamingStart>(parsed.value());
    ASSERT_TRUE(start.from);
    EXPECT_EQ(start.from->value, "peer-1");
}

TEST(SignalingProtocol, WatchMessagesRequireTargetPeer)
{
    const auto parsed = signaling::parse(signaling::dump(
        signaling::WatchStart { driscord::PeerId { "publisher" } }));
    ASSERT_TRUE(parsed);
    const auto& watch = std::get<signaling::WatchStart>(parsed.value());
    EXPECT_EQ(watch.peer_id.value, "publisher");

    EXPECT_FALSE(signaling::parse(R"({"type":"watch_start"})"));
    EXPECT_FALSE(signaling::parse(
        R"({"type":"watch_stop","peerId":""})"));
}

TEST(SignalingProtocol, RejectsUnknownOrIncompleteMessage)
{
    const auto unknown = signaling::parse(R"({"type":"bogus"})");
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error(), signaling::ParseError::UnknownType);

    const auto incomplete = signaling::parse(R"({"type":"candidate","candidate":"x"})");
    ASSERT_FALSE(incomplete);
    EXPECT_EQ(incomplete.error(), signaling::ParseError::MissingField);
}
