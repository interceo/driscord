#pragma once

// The SML transition table for a server-side session. Separated from
// session_fsm.hpp so <boost/sml.hpp> is pulled into exactly one translation
// unit chain (ws_server.cpp and the FSM unit test).

#include "session_fsm.hpp"

#include <boost/sml.hpp>

namespace session_fsm {

struct Machine {
    auto operator()() const
    {
        using namespace boost::sml;

        const auto accept = [](Actions& a, const OfferReceived& e) {
            a.accept_offer(e.sdp);
        };
        // Renegotiation. Closing first is the whole point: without it the
        // previous PeerConnection is orphaned, keeping its ICE ports bound and
        // leaving dead entries in the channel map. The client takes this path
        // every time it recovers from a media failure.
        const auto renegotiate = [](Actions& a, const OfferReceived& e) {
            a.close_peer_connection();
            a.accept_offer(e.sdp);
        };
        const auto candidate = [](Actions& a, const RemoteCandidate& e) {
            a.apply_candidate(e.candidate, e.mid);
        };
        const auto close = [](Actions& a) { a.close_peer_connection(); };
        const auto drop_candidate = [](Actions& a) { a.log_ignored("candidate"); };

        return make_transition_table(
            // clang-format off
            *"awaiting_offer"_s + event<OfferReceived>    / accept          = "negotiating"_s,

            "negotiating"_s     + event<RemoteCandidate>  / candidate       = "negotiating"_s,
            "negotiating"_s     + event<ChannelOpened>                      = "established"_s,
            "negotiating"_s     + event<OfferReceived>    / renegotiate     = "negotiating"_s,
            "negotiating"_s     + event<PcFailed>         / close           = "awaiting_offer"_s,

            "established"_s     + event<RemoteCandidate>  / candidate       = "established"_s,
            // A further channel opening on the same connection is normal —
            // the client creates four of them.
            "established"_s     + event<ChannelOpened>                      = "established"_s,
            "established"_s     + event<OfferReceived>    / renegotiate     = "negotiating"_s,
            "established"_s     + event<PcFailed>         / close           = "awaiting_offer"_s,

            "awaiting_offer"_s  + event<SessionClosing>   / close           = "closed"_s,
            "negotiating"_s     + event<SessionClosing>   / close           = "closed"_s,
            "established"_s     + event<SessionClosing>   / close           = "closed"_s,

            // Candidates routinely arrive before the offer or after teardown.
            // SML only flags events absent from the table as "unexpected", so
            // these would otherwise vanish without a trace.
            "awaiting_offer"_s  + event<RemoteCandidate>  / drop_candidate  = "awaiting_offer"_s,
            "closed"_s          + event<RemoteCandidate>  / drop_candidate  = "closed"_s,
            "closed"_s          + event<OfferReceived>    / drop_candidate  = "closed"_s
            // clang-format on
        );
    }
};

} // namespace session_fsm
