#include <gtest/gtest.h>

#include "utils/vector_view.hpp"

#include <cstdint>
#include <vector>

TEST(VectorView, DataAndSize) {
    std::vector<int> v = {1, 2, 3};
    utils::vector_view<int> view(v.data(), v.size());

    EXPECT_EQ(view.data(), v.data());
    EXPECT_EQ(view.size(), 3u);
}

TEST(VectorView, CdataMatchesData) {
    std::vector<int> v = {10, 20};
    utils::vector_view<int> view(v.data(), v.size());

    EXPECT_EQ(view.cdata(), view.data());
}

TEST(VectorView, ConstElements) {
    const std::vector<uint8_t> v = {0xAA, 0xBB, 0xCC};
    utils::vector_view<const uint8_t> view(v.data(), v.size());

    EXPECT_EQ(view.size(), 3u);
    EXPECT_EQ(view.data()[0], 0xAA);
    EXPECT_EQ(view.data()[2], 0xCC);
}

TEST(VectorView, EmptyView) {
    utils::vector_view<int> view(nullptr, 0);

    EXPECT_EQ(view.size(), 0u);
    EXPECT_EQ(view.data(), nullptr);
}

TEST(VectorView, SubView) {
    std::vector<int> v = {10, 20, 30, 40, 50};
    // View into middle of vector
    utils::vector_view<int> view(v.data() + 1, 3);

    EXPECT_EQ(view.size(), 3u);
    EXPECT_EQ(view.data()[0], 20);
    EXPECT_EQ(view.data()[1], 30);
    EXPECT_EQ(view.data()[2], 40);
}

TEST(VectorView, MutationThroughView) {
    std::vector<int> v = {1, 2, 3};
    utils::vector_view<int> view(v.data(), v.size());

    view.data()[1] = 99;
    EXPECT_EQ(v[1], 99);
}
