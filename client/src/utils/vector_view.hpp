#pragma once

#include <cstdint>

namespace utils {

template <class T> class vector_view {
    T* base_;
    size_t size_;

public:
    explicit vector_view(T* base, const size_t size)
        : base_(base)
        , size_(size) {}

    T* data() const noexcept { return base_; }
    const T* cdata() const noexcept { return base_; }

    size_t size() const noexcept { return size_; }
};

} // namespace utils