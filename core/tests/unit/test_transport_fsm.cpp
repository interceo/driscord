#include "transport_fsm_table.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace transport_fsm;

namespace {

// Records what the machine asked for, so a test can assert on side effects
// without a network, a WebSocket or a PeerConnection.
struct RecordingActions : Actions {
    int created = 0;
    int closed = 0;
    std::vector<std::string> answers;
    std::vector<std::string> candidates;
    std::vector<std::string> ignored;

    void create_peer_connection() override { ++created; }
    void apply_answer(const std::string& sdp) override { answers.push_back(sdp); }
    void apply_candidate(const std::string& candidate,
        const std::string& mid) override
    {
        candidates.push_back(candidate + "/" + mid);
    }
    void close_peer_connection() override { ++closed; }
    void log_ignored(const char* what) override { ignored.emplace_back(what); }
};

// Bundles the machine with its dependencies so tests read as a script of
// events rather than as setup.
struct Harness {
    RecordingActions actions;
    Backoff backoff;
    boost::sml::sm<Machine> sm { static_cast<Actions&>(actions), backoff };

    template <class E>
    void send(E e)
    {
        sm.process_event(e);
    }

    template <class S>
    bool in(const S& state) const
    {
        return sm.is(state);
    }

    // Drives the machine to a live media connection.
    void reach_connected()
    {
        send(ConnectRequested {});
        send(WsOpened {});
        send(PcConnected {});
    }
};

} // namespace

using boost::sml::operator""_s;

TEST(TransportFsm, StartsIdleAndOffersOnceSignalingIsUp)
{
    Harness h;
    EXPECT_TRUE(h.in("idle"_s));

    h.send(ConnectRequested {});
    EXPECT_TRUE(h.in("waiting_ws"_s));
    EXPECT_EQ(h.actions.created, 0) << "must not negotiate before the WS is open";

    h.send(WsOpened {});
    EXPECT_TRUE(h.in("negotiating"_s));
    EXPECT_EQ(h.actions.created, 1);

    h.send(PcConnected {});
    EXPECT_TRUE(h.in("connected"_s));
}

TEST(TransportFsm, AnswerAndCandidateReachThePeerConnectionWhileNegotiating)
{
    Harness h;
    h.send(ConnectRequested {});
    h.send(WsOpened {});

    h.send(AnswerReceived { "v=0-sdp" });
    h.send(RemoteCandidate { "cand:1", "0" });

    ASSERT_EQ(h.actions.answers.size(), 1u);
    EXPECT_EQ(h.actions.answers.front(), "v=0-sdp");
    ASSERT_EQ(h.actions.candidates.size(), 1u);
    EXPECT_EQ(h.actions.candidates.front(), "cand:1/0");
    EXPECT_TRUE(h.actions.ignored.empty());
}

// Signaling that arrives before there is anything to apply it to must be
// reported, not silently swallowed — that is the failure mode the machine
// exists to make visible.
TEST(TransportFsm, OutOfPhaseSignalingIsIgnoredAndLogged)
{
    Harness h;

    h.send(AnswerReceived { "late" });
    h.send(RemoteCandidate { "cand:9", "0" });

    EXPECT_TRUE(h.in("idle"_s));
    EXPECT_TRUE(h.actions.answers.empty());
    EXPECT_TRUE(h.actions.candidates.empty());
    EXPECT_EQ(h.actions.ignored.size(), 2u);
}

TEST(TransportFsm, MediaFailureTearsDownAndSchedulesRetry)
{
    Harness h;
    h.reach_connected();

    h.send(PcFailed { 1000 });

    EXPECT_TRUE(h.in("reconnecting"_s));
    EXPECT_EQ(h.actions.closed, 1);
    EXPECT_EQ(h.backoff.delay_ms, Backoff::kFirstDelayMs);
    EXPECT_EQ(h.backoff.due_at_ms, 1000 + Backoff::kFirstDelayMs);
}

TEST(TransportFsm, RetryWaitsForTheBackoffToElapse)
{
    Harness h;
    h.reach_connected();
    h.send(PcFailed { 1000 });
    const int created_before = h.actions.created;

    // Due at 2000: anything earlier must not renegotiate.
    h.send(Tick { 1500 });
    EXPECT_TRUE(h.in("reconnecting"_s));
    EXPECT_EQ(h.actions.created, created_before);

    h.send(Tick { 2000 });
    EXPECT_TRUE(h.in("negotiating"_s));
    EXPECT_EQ(h.actions.created, created_before + 1);
}

TEST(TransportFsm, BackoffGrowsPerFailureAndIsCapped)
{
    Harness h;
    h.reach_connected();

    int64_t now = 0;
    std::vector<int64_t> delays;
    for (int i = 0; i < 6; ++i) {
        h.send(PcFailed { now });
        delays.push_back(h.backoff.delay_ms);
        now = h.backoff.due_at_ms;
        h.send(Tick { now }); // back to negotiating
    }

    EXPECT_EQ(delays[0], 1000);
    EXPECT_EQ(delays[1], 2000);
    EXPECT_EQ(delays[2], 4000);
    EXPECT_EQ(delays[3], 8000);
    EXPECT_EQ(delays[4], Backoff::kMaxDelayMs) << "must be capped";
    EXPECT_EQ(delays[5], Backoff::kMaxDelayMs);
}

TEST(TransportFsm, SuccessfulReconnectResetsTheBackoff)
{
    Harness h;
    h.reach_connected();

    h.send(PcFailed { 0 });
    h.send(Tick { Backoff::kFirstDelayMs });
    ASSERT_TRUE(h.in("negotiating"_s));

    h.send(PcConnected {});
    EXPECT_TRUE(h.in("connected"_s));
    EXPECT_EQ(h.backoff.delay_ms, 0);

    // A later failure therefore starts from the shortest delay again.
    h.send(PcFailed { 5000 });
    EXPECT_EQ(h.backoff.delay_ms, Backoff::kFirstDelayMs);
}

// Renegotiation travels over the WebSocket, so losing it ends the session
// instead of retrying forever.
TEST(TransportFsm, LosingSignalingStopsTheRetryLoop)
{
    Harness h;
    h.reach_connected();
    h.send(PcFailed { 0 });
    ASSERT_TRUE(h.in("reconnecting"_s));

    h.send(WsClosed {});
    EXPECT_TRUE(h.in("idle"_s));

    const int created_before = h.actions.created;
    h.send(Tick { 999999 });
    EXPECT_TRUE(h.in("idle"_s));
    EXPECT_EQ(h.actions.created, created_before) << "must not reconnect after WS loss";
}

TEST(TransportFsm, DisconnectFromAnyPhaseEndsInIdle)
{
    {
        Harness h;
        h.send(ConnectRequested {});
        h.send(DisconnectRequested {});
        EXPECT_TRUE(h.in("idle"_s));
    }
    {
        Harness h;
        h.reach_connected();
        h.send(DisconnectRequested {});
        EXPECT_TRUE(h.in("idle"_s));
        EXPECT_EQ(h.actions.closed, 1);
    }
    {
        Harness h;
        h.reach_connected();
        h.send(PcFailed { 0 });
        h.send(DisconnectRequested {});
        EXPECT_TRUE(h.in("idle"_s));
    }
}

TEST(TransportFsm, ReconnectCycleCanRepeat)
{
    Harness h;
    h.reach_connected();

    int64_t now = 0;
    for (int i = 0; i < 3; ++i) {
        h.send(PcFailed { now });
        ASSERT_TRUE(h.in("reconnecting"_s)) << "cycle " << i;
        now = h.backoff.due_at_ms;
        h.send(Tick { now });
        ASSERT_TRUE(h.in("negotiating"_s)) << "cycle " << i;
        h.send(PcConnected {});
        ASSERT_TRUE(h.in("connected"_s)) << "cycle " << i;
    }

    EXPECT_EQ(h.actions.closed, 3);
    EXPECT_EQ(h.actions.created, 4); // initial + one per recovery
}
