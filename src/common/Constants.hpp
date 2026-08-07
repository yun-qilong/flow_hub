#pragma once

#include "generated/Types.hpp"
#include <cstddef>
#include <cstdint>

namespace common
{

constexpr common::GTID kInvalidGtid = 0xFFFF;
constexpr uint16_t kMaxClientsPerAccess = 64;

constexpr uint8_t kJudgeIndex = 0xFE;
constexpr size_t kMaxDebateAICount = 8;
constexpr size_t kTopicBaseJsonSize = 10485760;
constexpr size_t kSystemPromptSize = 4096;
constexpr size_t kResponseJsonSize = 4096;
constexpr size_t kModelNameSize = 64;
constexpr size_t kApiUrlSize = 128;
constexpr size_t kApiKeySize = 128;
constexpr double kContextRedundancyFactor = 1.1;

} // namespace common
