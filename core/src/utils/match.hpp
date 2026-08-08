#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace utils {

// Overload set built from a pack of callables — the classic std::visit helper.
// Each callable contributes one operator(); together they form a visitor whose
// best-matching overload is chosen for the active alternative.
template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

// Pattern-match a variant on the argument types of the supplied lambdas:
//
//   utils::Match(msg,
//       [](const Welcome& w) { ... },
//       [](const PeerLeft& p) { ... });
//
// Supplying one lambda per alternative makes the match exhaustive — adding a new
// alternative to the variant then fails to compile until it is handled, which a
// hand-written `if constexpr` chain does not give you. To ignore the rest on
// purpose, add a generic `[](auto&&) {}` catch-all (it loses to any exact match).
template <class Variant, class... Fs>
decltype(auto) Match(Variant&& v, Fs&&... fs)
{
    return std::visit(Overloaded<std::decay_t<Fs>...> { std::forward<Fs>(fs)... },
        std::forward<Variant>(v));
}

} // namespace utils
