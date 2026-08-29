#include <gtest/gtest.h>

#include "utils/match.hpp"

#include <string>
#include <variant>

TEST(Match, DispatchesByActiveType)
{
    std::variant<int, std::string, double> v = std::string { "hi" };

    const char* tag = utils::Match(
        v,
        [](int) { return "int"; },
        [](const std::string&) { return "string"; },
        [](double) { return "double"; });

    EXPECT_STREQ(tag, "string");
}

TEST(Match, PropagatesReturnValue)
{
    std::variant<int, double> v = 21;

    int r = utils::Match(
        v,
        [](int i) { return i * 2; },
        [](double d) { return static_cast<int>(d); });

    EXPECT_EQ(r, 42);
}

TEST(Match, CatchAllYieldsToExactMatch)
{
    std::variant<int, char, long> v = 'x';

    int hit = 0;
    utils::Match(
        v,
        [&](long) { hit = 1; },
        [&](auto&&) { hit = 2; });
    EXPECT_EQ(hit, 2);

    v = 7L;
    utils::Match(
        v,
        [&](long) { hit = 1; },
        [&](auto&&) { hit = 2; });
    EXPECT_EQ(hit, 1);
}

TEST(Match, OverloadedIsStandaloneVisitor)
{
    std::variant<int, std::string> v = 5;

    int r = std::visit(
        utils::Overloaded {
            [](int i) { return i; },
            [](const std::string& s) { return static_cast<int>(s.size()); },
        },
        v);

    EXPECT_EQ(r, 5);
}
