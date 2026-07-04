#pragma once

#include "generated/type/AppType.hpp"
#include <cstdint>

namespace common
{

class SessionFlags
{
  public:
    enum class BitFlags : uint8_t
    {
        needAckBit = 0x01,
    };

    constexpr SessionFlags() : flags_(0) {}

    template <AppType AT>
    static constexpr SessionFlags make()
    {
        uint8_t v = 0;
        switch (AT)
        {
        case AppType::AiChat:
        case AppType::AiDiscussion:
            v = static_cast<uint8_t>(BitFlags::needAckBit);
            break;
        default:
            break;
        }
        return SessionFlags{v};
    }

    constexpr bool isNeedAck() const
    {
        return flags_ & static_cast<uint8_t>(BitFlags::needAckBit);
    }

    template <class Inspector>
    friend bool inspect(Inspector &f, SessionFlags &x)
    {
        return f.value(x.flags_);
    }

  private:
    uint8_t flags_;
    explicit constexpr SessionFlags(uint8_t v) : flags_(v) {}
};

} // namespace common
