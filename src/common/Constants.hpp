#pragma once

#include "generated/Types.hpp"
#include <cstdint>

namespace common
{

constexpr uint8_t kUserIdBits = 8;
constexpr uint8_t kAppTypeBits = 8;

constexpr uint16_t kMaxUsers = 64;
constexpr uint16_t kMaxAppTypes = 64;
constexpr uint16_t kMaxAccessTypes = 64;
constexpr uint16_t kMaxUid = 65535;
constexpr uint16_t kInvalidUid = 0xFFFF;
constexpr common::GTID kInvalidGtid = 0xFFFF;
constexpr uint16_t kMaxClientsPerAccess = 64;
constexpr uint16_t kMaxGtidsPerUser = 128;
constexpr uint8_t kMaxUsernameLen = 12;

constexpr common::ConnectionId kInvalidConnectionId = 0xFF;
constexpr common::UserId kInvalidUserId = 0xFF;

} // namespace common
