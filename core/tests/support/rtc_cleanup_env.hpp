#pragma once

#include <gtest/gtest.h>
#include <rtc/rtc.hpp>

namespace test_util {

// libdatachannel keeps a process-wide thread pool alive behind its own static
// state. Left alone, that state is torn down concurrently with the static
// destructors at process exit, which crashes intermittently once a binary has
// churned through many PeerConnections — as the integration tests do, since
// they run both client and server in-process.
//
// rtc::Cleanup() joins those threads deterministically. Registering it as a
// gtest environment runs it after the last test but while the runtime is still
// fully alive.
class RtcCleanupEnvironment : public ::testing::Environment {
public:
    void TearDown() override { rtc::Cleanup().wait(); }
};

// Registering at namespace scope means it happens before main() runs.
inline const auto* rtc_cleanup_env
    = ::testing::AddGlobalTestEnvironment(new RtcCleanupEnvironment);

} // namespace test_util
