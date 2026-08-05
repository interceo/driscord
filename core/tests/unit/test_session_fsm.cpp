#include "session_fsm_table.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace session_fsm;

namespace {

struct RecordingActions : Actions {
    std::vector<std::string> offers;
    std::vector<std::string> candidates;
    int closed = 0;
    std::vector<std::string> ignored;

    void accept_offer(const std::string& sdp) override { offers.push_back(sdp); }
    void apply_candidate(const std::string& candidate,
        const std::string& mid) override
    {
        candidates.push_back(candidate + "/" + mid);
    }
    void close_peer_connection() override { ++closed; }
    void log_ignored(const char* what) override { ignored.emplace_back(what); }
};

struct Harness {
    RecordingActions actions;
    boost::sml::sm<Machine> sm { static_cast<Actions&>(actions) };

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

    void reach_established()
    {
        send(OfferReceived { "sdp-1" });
        send(ChannelOpened {});
    }
};

} // namespace

using boost::sml::operator""_s;

TEST(SessionFsm, AnswersTheFirstOfferAndSettlesOnceAChannelOpens)
{
    Harness h;
    EXPECT_TRUE(h.in("awaiting_offer"_s));

    h.send(OfferReceived { "sdp-1" });
    EXPECT_TRUE(h.in("negotiating"_s));
    ASSERT_EQ(h.actions.offers.size(), 1u);
    EXPECT_EQ(h.actions.offers.front(), "sdp-1");
    EXPECT_EQ(h.actions.closed, 0) << "nothing to tear down on a first offer";

    h.send(ChannelOpened {});
    EXPECT_TRUE(h.in("established"_s));
}

// The client opens four channels on one connection, so this must not be
// treated as anything unusual.
TEST(SessionFsm, FurtherChannelsOnTheSameConnectionKeepItEstablished)
{
    Harness h;
    h.reach_established();

    h.send(ChannelOpened {});
    h.send(ChannelOpened {});
    h.send(ChannelOpened {});

    EXPECT_TRUE(h.in("established"_s));
    EXPECT_EQ(h.actions.offers.size(), 1u) << "no renegotiation should happen";
    EXPECT_EQ(h.actions.closed, 0);
}

// The bug this machine was introduced to fix: a second offer used to build a
// new PeerConnection while the old one stayed alive, holding its ICE ports and
// leaving dead entries in the channel map.
TEST(SessionFsm, RenegotiationClosesThePreviousPeerConnectionFirst)
{
    Harness h;
    h.reach_established();

    h.send(OfferReceived { "sdp-2" });

    EXPECT_TRUE(h.in("negotiating"_s));
    EXPECT_EQ(h.actions.closed, 1) << "the old connection must be torn down";
    ASSERT_EQ(h.actions.offers.size(), 2u);
    EXPECT_EQ(h.actions.offers.back(), "sdp-2");
}

TEST(SessionFsm, RenegotiationWhileStillNegotiatingAlsoClosesFirst)
{
    Harness h;
    h.send(OfferReceived { "sdp-1" });
    ASSERT_TRUE(h.in("negotiating"_s));

    h.send(OfferReceived { "sdp-2" });

    EXPECT_TRUE(h.in("negotiating"_s));
    EXPECT_EQ(h.actions.closed, 1);
    EXPECT_EQ(h.actions.offers.size(), 2u);
}

// A client that keeps losing its media path re-offers each time; the server
// must not accumulate connections across those cycles.
TEST(SessionFsm, RepeatedRenegotiationClosesOncePerCycle)
{
    Harness h;
    h.reach_established();

    for (int i = 0; i < 5; ++i) {
        h.send(OfferReceived { "sdp" });
        h.send(ChannelOpened {});
        ASSERT_TRUE(h.in("established"_s)) << "cycle " << i;
    }

    EXPECT_EQ(h.actions.closed, 5) << "exactly one teardown per renegotiation";
    EXPECT_EQ(h.actions.offers.size(), 6u); // initial + 5
}

TEST(SessionFsm, CandidatesReachThePeerConnectionOnceNegotiating)
{
    Harness h;
    h.send(OfferReceived { "sdp-1" });

    h.send(RemoteCandidate { "cand:1", "0" });
    h.send(ChannelOpened {});
    h.send(RemoteCandidate { "cand:2", "0" });

    ASSERT_EQ(h.actions.candidates.size(), 2u);
    EXPECT_EQ(h.actions.candidates.front(), "cand:1/0");
    EXPECT_EQ(h.actions.candidates.back(), "cand:2/0");
    EXPECT_TRUE(h.actions.ignored.empty());
}

TEST(SessionFsm, CandidateBeforeAnyOfferIsIgnoredAndLogged)
{
    Harness h;

    h.send(RemoteCandidate { "cand:1", "0" });

    EXPECT_TRUE(h.in("awaiting_offer"_s));
    EXPECT_TRUE(h.actions.candidates.empty());
    EXPECT_EQ(h.actions.ignored.size(), 1u);
}

TEST(SessionFsm, MediaFailureReturnsToAwaitingOffer)
{
    Harness h;
    h.reach_established();

    h.send(PcFailed {});

    EXPECT_TRUE(h.in("awaiting_offer"_s));
    EXPECT_EQ(h.actions.closed, 1);

    // The client's reconnect then arrives as a fresh offer.
    h.send(OfferReceived { "sdp-after-failure" });
    EXPECT_TRUE(h.in("negotiating"_s));
    EXPECT_EQ(h.actions.closed, 1) << "nothing left to close after the failure";
}

TEST(SessionFsm, ClosingTearsDownFromAnyPhase)
{
    {
        Harness h;
        h.send(SessionClosing {});
        EXPECT_TRUE(h.in("closed"_s));
    }
    {
        Harness h;
        h.send(OfferReceived { "sdp" });
        h.send(SessionClosing {});
        EXPECT_TRUE(h.in("closed"_s));
        EXPECT_EQ(h.actions.closed, 1);
    }
    {
        Harness h;
        h.reach_established();
        h.send(SessionClosing {});
        EXPECT_TRUE(h.in("closed"_s));
        EXPECT_EQ(h.actions.closed, 1);
    }
}

TEST(SessionFsm, SignalingAfterCloseIsIgnored)
{
    Harness h;
    h.reach_established();
    h.send(SessionClosing {});
    const int closed_before = h.actions.closed;

    h.send(OfferReceived { "sdp-late" });
    h.send(RemoteCandidate { "cand:late", "0" });

    EXPECT_TRUE(h.in("closed"_s));
    EXPECT_EQ(h.actions.closed, closed_before) << "a closed session must stay closed";
    EXPECT_EQ(h.actions.offers.size(), 1u) << "no connection may be built after close";
    EXPECT_EQ(h.actions.ignored.size(), 2u);
}
