#pragma once

#include <cstdint>

namespace utils
{

enum class LogLevel : uint8_t
{
    ERR = 0,
    WRN = 1,
    INFO = 2,
    DBG = 3,
};

enum class LogFeature : uint8_t
{
    AICHAT = 0,
    AIDISCUSS = 1
};

} // namespace utils
