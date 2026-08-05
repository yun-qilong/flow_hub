#pragma once

#include "generated/Types.hpp"
#include <cstdint>

namespace common
{

constexpr common::GTID kInvalidGtid = 0xFFFF;
constexpr uint16_t kMaxClientsPerAccess = 64;

} // namespace common
