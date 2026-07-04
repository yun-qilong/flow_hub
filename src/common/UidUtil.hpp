#pragma once

#include <cstdint>
#include "common/Constants.hpp"
#include "generated/type/AppType.hpp"

namespace common
{

constexpr uint16_t makeUid(uint8_t userId, common::AppType appType)
{
  return (static_cast<uint16_t>(userId) << kUserIdBits)
         | static_cast<uint8_t>(appType);
}

constexpr uint8_t getUserId(uint16_t uid)
{
  return static_cast<uint8_t>(uid >> kUserIdBits);
}

constexpr common::AppType getAppType(uint16_t uid)
{
  return static_cast<common::AppType>(uid & ((1 << kAppTypeBits) - 1));
}

} // namespace common
