#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace utils {

template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

template <class Variant, class... Fs>
decltype(auto) Match(Variant&& v, Fs&&... fs)
{
    return std::visit(Overloaded<std::decay_t<Fs>...> { std::forward<Fs>(fs)... },
        std::forward<Variant>(v));
}

}
