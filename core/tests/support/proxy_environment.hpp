#pragma once

#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace test_util {

// The core tests also run cross-built under Wine, so the environment is
// touched through the spelling each platform actually has. On Windows an empty
// value removes the variable instead of exporting it empty; both read back as
// unset, which is all any test asserts.
inline void put_environment(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

inline void drop_environment(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

// Clears every proxy variable for the lifetime of the guard and restores what
// the shell had afterwards.
//
// Clearing rather than merely setting is the point: a developer box or a CI
// image that already exports HTTP_PROXY and a NO_PROXY covering 127.0.0.1
// would otherwise route the loopback fixture straight past the test proxy, and
// the test would pass or fail for reasons that have nothing to do with the
// code under test.
class ScopedProxyEnvironment {
public:
    ScopedProxyEnvironment()
    {
        for (const char* name : kNames) {
            const char* value = std::getenv(name);
            saved_.emplace_back(name,
                value ? std::optional<std::string>(value) : std::nullopt);
            drop_environment(name);
        }
    }

    ~ScopedProxyEnvironment()
    {
        for (const auto& [name, value] : saved_) {
            if (value) {
                put_environment(name, value->c_str());
            } else {
                drop_environment(name);
            }
        }
    }

    ScopedProxyEnvironment(const ScopedProxyEnvironment&) = delete;
    ScopedProxyEnvironment& operator=(const ScopedProxyEnvironment&) = delete;

    static void set(const char* name, const std::string& value)
    {
        put_environment(name, value.c_str());
    }

private:
    static constexpr std::array<const char*, 8> kNames { "HTTPS_PROXY",
        "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy",
        "NO_PROXY", "no_proxy" };

    std::vector<std::pair<const char*, std::optional<std::string>>> saved_;
};

}
