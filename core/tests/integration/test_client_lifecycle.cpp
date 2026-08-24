// GoogleWebRtcClient state and preference surface.
//
// The coordinator is the largest single file in the client and had no direct
// test — it was only reached transitively through the media integration gate.
// The methods below are pure preference/state operations that never require an
// active media session: they update the coordinator's own maps and only touch
// a live session when one exists. That makes them testable without opening the
// microphone, so they run headless like every other gate test.
//
// The load-bearing case is the documented invariant the gap report calls out
// and nothing tested: per-peer volume/mute are the *user's* settings for that
// person, so they survive the peer merely pausing their stream
// (peer_stopped_streaming) but are cleared when the peer leaves entirely
// (remove_peer).

#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "wait_helpers.hpp"
#include "webrtc/google_webrtc_client.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {

// A client with an unconnected transport and no-op callbacks: enough to drive
// the preference surface without any signaling or media.
class ClientLifecycleTest : public ::testing::Test {
protected:
    Transport transport_;
    GoogleWebRtcClient client_ { transport_, GoogleWebRtcClient::Callbacks { } };
};

TEST_F(ClientLifecycleTest, VoiceStateDefaultsAndToggles)
{
    EXPECT_FALSE(client_.muted());
    EXPECT_FALSE(client_.deafened());

    client_.set_muted(true);
    EXPECT_TRUE(client_.muted());
    client_.set_muted(false);
    EXPECT_FALSE(client_.muted());

    client_.set_deafened(true);
    EXPECT_TRUE(client_.deafened());
    client_.set_deafened(false);
    EXPECT_FALSE(client_.deafened());
}

TEST_F(ClientLifecycleTest, MasterVolumeIsClamped)
{
    client_.set_master_volume(1.5f);
    EXPECT_FLOAT_EQ(client_.master_volume(), 1.5f);

    client_.set_master_volume(-1.0f);
    EXPECT_GE(client_.master_volume(), 0.0f);

    client_.set_master_volume(100.0f);
    EXPECT_LE(client_.master_volume(), 2.0f);
}

TEST_F(ClientLifecycleTest, PerPeerVolumeAndMuteAreIndependent)
{
    EXPECT_FLOAT_EQ(client_.peer_volume("alice"), 1.0f); // default
    EXPECT_FALSE(client_.peer_muted("alice"));

    client_.set_peer_volume("alice", 0.25f);
    client_.set_peer_muted("bob", true);

    EXPECT_FLOAT_EQ(client_.peer_volume("alice"), 0.25f);
    EXPECT_FLOAT_EQ(client_.peer_volume("bob"), 1.0f);
    EXPECT_TRUE(client_.peer_muted("bob"));
    EXPECT_FALSE(client_.peer_muted("alice"));
}

TEST_F(ClientLifecycleTest, PreferencesSurviveAStopButNotALeave)
{
    client_.set_peer_volume("carol", 0.4f);
    client_.set_peer_muted("carol", true);

    // Carol stops publishing but is still in the channel: her per-peer
    // settings are the user's choice about her, not about the stream.
    client_.peer_stopped_streaming("carol");
    EXPECT_FLOAT_EQ(client_.peer_volume("carol"), 0.4f);
    EXPECT_TRUE(client_.peer_muted("carol"));

    // Carol leaves the channel entirely: her settings are dropped, so a
    // future namesake starts from defaults.
    client_.remove_peer("carol");
    EXPECT_FLOAT_EQ(client_.peer_volume("carol"), 1.0f);
    EXPECT_FALSE(client_.peer_muted("carol"));
}

TEST_F(ClientLifecycleTest, VoiceStatsJsonReportsDisconnectedBeforeStart)
{
    const std::string stats = client_.voice_stats_json();
    // Valid JSON, and honest about there being no session yet.
    const auto parsed = nlohmann::json::parse(stats, nullptr, false);
    ASSERT_FALSE(parsed.is_discarded()) << stats;
    EXPECT_FALSE(parsed.value("connected", true));
    EXPECT_EQ(parsed.value("rttMs", 0), -1);
}

TEST_F(ClientLifecycleTest, RemovingAnUnknownPeerIsHarmless)
{
    client_.remove_peer("nobody");
    client_.peer_stopped_streaming("nobody");
    SUCCEED();
}

// The voice path end to end on the coordinator's own platform-ADM runtime —
// the case DRISCORD-9 unblocked. Before the dummy-device fallback this was
// untestable headless: start_voice on a machine without an audio server
// tripped a fatal RTC_CHECK inside the voice engine and killed the process.
// Meaningful in both environments: with a real audio device it exercises the
// production path, without one it exercises the fallback.
TEST(ClientVoiceSession, StartsVoiceHeadlessWithoutAborting)
{
    test_util::SignalingServerFixture server;
    Transport transport;
    GoogleWebRtcClient client { transport,
        GoogleWebRtcClient::Callbacks { } };

    ASSERT_TRUE(transport.connect(server.ws_url()));
    ASSERT_TRUE(test_util::wait_for_local_id(transport));

    client.start_voice();
    const auto deadline
        = std::chrono::steady_clock::now() + test_util::kDefaultTimeout;
    bool connected = false;
    while (!connected && std::chrono::steady_clock::now() < deadline) {
        const auto parsed = nlohmann::json::parse(
            client.voice_stats_json(), nullptr, false);
        ASSERT_FALSE(parsed.is_discarded());
        connected = parsed.value("connected", false);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(connected)
        << "voice session never came up on the platform-ADM runtime";

    // Whichever way this environment resolved, the availability signal must
    // be queryable so the UI can explain a silent microphone.
    const bool available = client.audio_device_available();
    if (!available) {
        EXPECT_TRUE(client.input_devices().empty());
        EXPECT_TRUE(client.output_devices().empty());
    }

    client.stop_voice();
    transport.disconnect();
}

} // namespace
