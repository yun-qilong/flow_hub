#pragma once

#include "common/BatchToken.hpp"
#include "common/Constants.hpp"
#include "utils/Result.hpp"

#include <array>
#include <chrono>
#include <cstdint>

namespace common
{

class BatchCounter
{
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr auto kTimeoutThreshold = std::chrono::seconds(5);

    utils::Result<BatchToken> allocate(uint8_t total);

    bool replyAndCheckDone(BatchToken token);

  private:
    struct Counter
    {
        void init(uint8_t total, uint16_t newEpoch, TimePoint now);

        uint8_t remaining = 0;
        uint16_t epoch = 0;
        TimePoint allocTime;
    };

    bool isActive(size_t index) const;
    bool isTimeout(size_t index) const;
    void reapTimeoutCounters();

    std::array<Counter, kMaxBatchCounterNum> counters_;
    uint16_t activeBitmap_ = 0;
    uint16_t epochCounter_ = 0;
};

} // namespace common
