#include <gtest/gtest.h>

#include "utils/expected.hpp"

#include <string>
#include <vector>

using utils::Expected;
using utils::Unexpected;

// --- Expected<T, E> ---

TEST(Expected, ValueConstruction)
{
    Expected<int> r(42);

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(*r, 42);
    EXPECT_EQ(r.value(), 42);
}

TEST(Expected, ErrorConstruction)
{
    Expected<int> r(Unexpected(std::string("fail")));

    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), "fail");
}

TEST(Expected, ImplicitValueConversion)
{
    Expected<int> r = 7;
    EXPECT_EQ(*r, 7);
}

TEST(Expected, MoveValue)
{
    Expected<std::vector<int>> r(std::vector<int> { 1, 2, 3 });

    auto v = std::move(r).value();
    EXPECT_EQ(v.size(), 3u);
}

TEST(Expected, MoveError)
{
    Expected<int> r(Unexpected(std::string("moved")));

    auto e = std::move(r).error();
    EXPECT_EQ(e, "moved");
}

TEST(Expected, ArrowOperator)
{
    Expected<std::string> r(std::string("hello"));
    EXPECT_EQ(r->size(), 5u);
}

TEST(Expected, ValueOr)
{
    Expected<int> ok(10);
    Expected<int> err(Unexpected(std::string("oops")));

    EXPECT_EQ(ok.value_or(0), 10);
    EXPECT_EQ(err.value_or(0), 0);
}

TEST(Expected, CustomErrorType)
{
    enum Err { NotFound,
        Timeout };

    Expected<int, Err> r(Unexpected(Err::Timeout));

    EXPECT_FALSE(r);
    EXPECT_EQ(r.error(), Err::Timeout);
}

// --- Expected<void, E> ---

TEST(ExpectedVoid, SuccessConstruction)
{
    Expected<void> r;

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ExpectedVoid, ErrorConstruction)
{
    Expected<void> r(Unexpected(std::string("init failed")));

    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), "init failed");
}

TEST(ExpectedVoid, MoveError)
{
    Expected<void> r(Unexpected(std::string("moved")));

    auto e = std::move(r).error();
    EXPECT_EQ(e, "moved");
}

TEST(ExpectedVoid, ReturnPattern)
{
    auto fn = [](bool ok) -> Expected<void> {
        if (!ok)
            return Unexpected(std::string("bad"));
        return { };
    };

    EXPECT_TRUE(fn(true));
    EXPECT_FALSE(fn(false));
    EXPECT_EQ(fn(false).error(), "bad");
}
