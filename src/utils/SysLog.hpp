#pragma once

#include "common/testable/TestableMacros.hpp"
#include "generated/LogConfig.hpp"
#include "utils/LogTypes.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace utils
{

// ===== Internal: printf → std::string (always compiled) =====

template <typename... Args>
inline std::string formatLog(const char *fmt, Args &&...args)
{
    if constexpr (sizeof...(Args) == 0)
    {
        return {fmt};
    }
    else
    {
        std::array<char, 512> buf{};
        auto n = snprintf(buf.data(), buf.size(), fmt, // NOLINT(cppcoreguidelines-pro-type-vararg)
                          std::forward<Args>(args)...);
        static_cast<void>(n);
        return {buf.data()};
    }
}

} // namespace utils

// ===== Routing Block (Mode B: no empty, default mock) =====

#if FLOWHUB_TEST_BUILD && !USE_ORIG(SysLog)

#include TESTABLE_MOCK("utils/ut/mocks/SysLogMock.hpp")

#else

namespace utils
{

class SysLog
{
  public:
    explicit SysLog(std::string logDir, uint32_t maxSizeKb);

    void log(LogLevel level, const std::string &msg);
    void logFeature(LogFeature feature, const std::string &msg);

  private:
    static bool shouldLog(LogLevel level)
    {
        return level <= kRuntimeMinLevel;
    }

    static const char *levelLabel(LogLevel level);
    static const char *featureLabel(LogFeature f);
    void write(const char *label, const std::string &msg);
    void checkRotation();
    void openNewFile();

    std::string logDir_;
    std::string currentFile_;
    size_t maxSizeBytes_;
    std::ofstream file_;
    std::mutex mutex_;
};

SysLog *createSysLog();

inline SysLog *&gSysLog()
{
    static SysLog *instance = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    return instance;
}

} // namespace utils

#endif

// ===== Log helpers (zero cognitive complexity at call sites) =====

namespace utils
{

template <LogLevel L>
inline void doLog(const std::string &msg)
{
    if constexpr (L <= kRuntimeMinLevel)
    {
        if (gSysLog() != nullptr)
        {
            gSysLog()->log(L, msg);
        }
    }
}

template <LogFeature F>
inline void doLogFeat(const std::string &msg)
{
    if constexpr (isLogFeatEnabled(F))
    {
        if (gSysLog() != nullptr)
        {
            gSysLog()->logFeature(F, msg);
        }
    }
}

} // namespace utils

// ===== Log Macros =====

#define LG_ERR(fmt, ...)                                                                           \
    ::utils::doLog<::utils::LogLevel::ERR>(::utils::formatLog(fmt, ##__VA_ARGS__))

#define LG_WRN(fmt, ...)                                                                           \
    ::utils::doLog<::utils::LogLevel::WRN>(::utils::formatLog(fmt, ##__VA_ARGS__))

#define LG_INFO(fmt, ...)                                                                          \
    ::utils::doLog<::utils::LogLevel::INFO>(::utils::formatLog(fmt, ##__VA_ARGS__))

#define LG_DBG(fmt, ...)                                                                           \
    ::utils::doLog<::utils::LogLevel::DBG>(::utils::formatLog(fmt, ##__VA_ARGS__))

#define LG_FEAT(feat, fmt, ...)                                                                    \
    ::utils::doLogFeat<::utils::LogFeature::feat>(::utils::formatLog(fmt, ##__VA_ARGS__))
