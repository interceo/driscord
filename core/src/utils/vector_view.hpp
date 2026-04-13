#pragma once

#include <cstddef>
#include <iterator>

namespace utils {

template <class T>
class vector_view {
    T* base_;
    size_t size_;

public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    explicit vector_view(T* base, size_t size) noexcept
        : base_(base)
        , size_(size)
    {
    }

    // Element access
    T* data() const noexcept { return base_; }
    const T* cdata() const noexcept { return base_; }
    T& operator[](size_t i) const noexcept { return base_[i]; }
    T& front() const noexcept { return base_[0]; }
    T& back() const noexcept { return base_[size_ - 1]; }

    // Capacity
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    // Iterators
    iterator begin() noexcept { return base_; }
    iterator end() noexcept { return base_ + size_; }
    const_iterator begin() const noexcept { return base_; }
    const_iterator end() const noexcept { return base_ + size_; }
    const_iterator cbegin() const noexcept { return base_; }
    const_iterator cend() const noexcept { return base_ + size_; }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }
};

} // namespace utils