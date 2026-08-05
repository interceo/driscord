#pragma once

// The SML transition table for the client's media connection. Separated from
// transport_fsm.hpp so that <boost/sml.hpp> is included by exactly one
// translation unit chain (transport.cpp and the FSM unit test), keeping it out
// of the public transport.hpp.

#include "transport_fsm.hpp"

#include <boost/sml.hpp>

namespace transport_fsm {

// --- transition table --------------------------------------------------------

struct Machine {
    auto operator()() const
    {
        using namespace boost::sml;

        const auto create_pc = [](Actions& a) { a.create_peer_connection(); };
        const auto close_pc = [](Actions& a) { a.close_peer_connection(); };

        const auto answer = [](Actions& a, const AnswerReceived& e) {
            a.apply_answer(e.sdp);
        };
        const auto candidate = [](Actions& a, const RemoteCandidate& e) {
            a.apply_candidate(e.candidate, e.mid);
        };

        // Media died. Tear the old connection down and wait out the backoff
        // before renegotiating over the still-open WebSocket.
        const auto fail = [](Actions& a, Backoff& b, const PcFailed& e) {
            a.close_peer_connection();
            b.arm(e.now_ms);
        };
        const auto settled = [](Backoff& b) { b.reset(); };
        const auto backoff_elapsed = [](Backoff& b, const Tick& e) {
            return b.elapsed(e.now_ms);
        };

        const auto drop_answer = [](Actions& a) { a.log_ignored("answer"); };
        const auto drop_candidate = [](Actions& a) { a.log_ignored("candidate"); };

        return make_transition_table(
            // clang-format off
            *"idle"_s          + event<ConnectRequested>                        = "waiting_ws"_s,

            "waiting_ws"_s     + event<WsOpened>              / create_pc       = "negotiating"_s,

            "negotiating"_s    + event<AnswerReceived>        / answer          = "negotiating"_s,
            "negotiating"_s    + event<RemoteCandidate>       / candidate       = "negotiating"_s,
            "negotiating"_s    + event<PcConnected>           / settled         = "connected"_s,
            "negotiating"_s    + event<PcFailed>              / fail            = "reconnecting"_s,

            "connected"_s      + event<RemoteCandidate>       / candidate       = "connected"_s,
            "connected"_s      + event<PcFailed>              / fail            = "reconnecting"_s,

            "reconnecting"_s   + event<Tick>[backoff_elapsed] / create_pc       = "negotiating"_s,

            // The signaling link is what renegotiation would travel over, so
            // losing it ends the media session outright.
            "waiting_ws"_s     + event<WsClosed>                                = "idle"_s,
            "negotiating"_s    + event<WsClosed>              / close_pc        = "idle"_s,
            "connected"_s      + event<WsClosed>              / close_pc        = "idle"_s,
            "reconnecting"_s   + event<WsClosed>              / close_pc        = "idle"_s,

            "waiting_ws"_s     + event<DisconnectRequested>                     = "idle"_s,
            "negotiating"_s    + event<DisconnectRequested>   / close_pc        = "idle"_s,
            "connected"_s      + event<DisconnectRequested>   / close_pc        = "idle"_s,
            "reconnecting"_s   + event<DisconnectRequested>   / close_pc        = "idle"_s,

            // Late or out-of-phase signaling. SML only reports an event as
            // "unexpected" when it appears nowhere in the table, so these
            // would otherwise be dropped without a trace.
            "idle"_s           + event<AnswerReceived>        / drop_answer     = "idle"_s,
            "idle"_s           + event<RemoteCandidate>       / drop_candidate  = "idle"_s,
            "waiting_ws"_s     + event<AnswerReceived>        / drop_answer     = "waiting_ws"_s,
            "waiting_ws"_s     + event<RemoteCandidate>       / drop_candidate  = "waiting_ws"_s,
            "reconnecting"_s   + event<AnswerReceived>        / drop_answer     = "reconnecting"_s,
            "reconnecting"_s   + event<RemoteCandidate>       / drop_candidate  = "reconnecting"_s
            // clang-format on
        );
    }
};

} // namespace transport_fsm
