#pragma once

#include <gtest/gtest.h>
#include <rtc/rtc.hpp>

namespace test_util {

class RtcCleanupEnvironment : public ::testing::Environment {
public:
    void TearDown() override { rtc::Cleanup().wait(); }
};

inline const auto* rtc_cleanup_env
    = ::testing::AddGlobalTestEnvironment(new RtcCleanupEnvironment);

}
