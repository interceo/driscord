#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace utils {

template <typename E>
struct Unexpected {
    E error;

    explicit Unexpected(const E& e)
        : error(e)
    {
    }
    explicit Unexpected(E&& e)
        : error(std::move(e))
    {
    }
};

template <typename E>
Unexpected(E) -> Unexpected<E>;

template <typename T, typename E = std::string>
class Expected {
public:
    Expected(const T& val)
        : storage_(std::in_place_index<0>, val)
    {
    }
    Expected(T&& val)
        : storage_(std::in_place_index<0>, std::move(val))
    {
    }

    // --- error constructor ---
    Expected(Unexpected<E> err)
        : storage_(std::in_place_index<1>, std::move(err.error))
    {
    }

    bool has_value() const { return storage_.index() == 0; }
    explicit operator bool() const { return has_value(); }

    T& value() & { return std::get<0>(storage_); }
    const T& value() const& { return std::get<0>(storage_); }
    T&& value() && { return std::get<0>(std::move(storage_)); }

    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }
    T&& operator*() && { return std::move(*this).value(); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    E& error() & { return std::get<1>(storage_); }
    const E& error() const& { return std::get<1>(storage_); }
    E&& error() && { return std::get<1>(std::move(storage_)); }

    T value_or(T&& fallback) const&
    {
        return has_value() ? value() : static_cast<T>(std::forward<T>(fallback));
    }
    T value_or(T&& fallback) &&
    {
        return has_value() ? std::move(*this).value()
                           : static_cast<T>(std::forward<T>(fallback));
    }

private:
    std::variant<T, E> storage_;
};

template <typename E>
class Expected<void, E> {
public:
    Expected()
        : error_()
    {
    }

    Expected(Unexpected<E> err)
        : error_(std::move(err.error))
    {
    }

    bool has_value() const { return !error_.has_value(); }
    explicit operator bool() const { return has_value(); }

    E& error() & { return *error_; }
    const E& error() const& { return *error_; }
    E&& error() && { return std::move(*error_); }

private:
    std::optional<E> error_;
};

} // namespace utils
