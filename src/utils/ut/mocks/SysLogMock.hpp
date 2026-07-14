#pragma once

#include "utils/LogTypes.hpp"

#include <gmock/gmock.h>

// Replaces utils::SysLog entirely in test builds.
// MockSysLog is standalone — does not inherit anything.

namespace utils
{

class MockSysLog
{
  public:
    MOCK_METHOD(void, log, (LogLevel, const std::string &));
    MOCK_METHOD(void, logFeature, (LogFeature, const std::string &));
};

inline MockSysLog *&gSysLog()
{
    static MockSysLog *instance =
        nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    return instance;
}

} // namespace utils

// ===== Convenience macros for UT EXPECT_CALL =====

#define EXPECT_LOG(level, times)                                                                   \
    EXPECT_CALL(*::utils::gSysLog(), log((level), ::testing::_)).Times(times)

#define EXPECT_LOG_FEAT(feat, times)                                                               \
    EXPECT_CALL(*::utils::gSysLog(), logFeature((feat), ::testing::_)).Times(times)
