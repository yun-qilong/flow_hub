#include "common/BatchCounter.hpp"

#include <iostream>

namespace common
{

void BatchCounter::Counter::init(uint8_t total, uint16_t newEpoch, TimePoint now)
{
    remaining = total;
    epoch = newEpoch;
    allocTime = now;
}

utils::Result<BatchToken> BatchCounter::allocate(uint8_t total)
{
    reapTimeoutCounters();

    uint16_t freeBitmap = ~activeBitmap_ & ((1 << kMaxBatchCounterNum) - 1);
    if (freeBitmap == 0)
    {
        return std::nullopt;
    }

    auto i = static_cast<size_t>(__builtin_ctz(freeBitmap));
    ++epochCounter_;
    counters_.at(i).init(total, epochCounter_, Clock::now());
    activeBitmap_ |= (1 << i);
    return utils::Result<BatchToken>(BatchToken{i, epochCounter_});
}

void BatchCounter::reapTimeoutCounters()
{
    uint16_t bitmap = activeBitmap_;
    while (bitmap)
    {
        size_t i = static_cast<size_t>(__builtin_ctz(bitmap));
        if (isTimeout(i))
        {
            std::cerr << "[BatchCounter] WARNING: counter index=" << i << " timed out, releasing\n";
            counters_.at(i).remaining = 0;
            activeBitmap_ &= ~(1U << i);
        }
        bitmap &= bitmap - 1;
    }
}

bool BatchCounter::replyAndCheckDone(BatchToken token)
{
    if (not isActive(token.index))
    {
        std::cerr << "[BatchCounter] WARNING: reply on inactive index=" << token.index
                  << " epoch=" << token.epoch << "\n";
        return false;
    }
    auto &c = counters_.at(token.index);
    if (c.epoch != token.epoch)
    {
        std::cerr << "[BatchCounter] WARNING: reply on stale token index=" << token.index
                  << " epoch=" << token.epoch << " (current=" << c.epoch << ")\n";
        return false;
    }
    --c.remaining;
    if (c.remaining == 0)
    {
        activeBitmap_ &= ~(1U << token.index);
        return true;
    }
    return false;
}

bool BatchCounter::isActive(size_t index) const
{
    return index < kMaxBatchCounterNum and (activeBitmap_ & (1U << index)) != 0;
}

bool BatchCounter::isTimeout(size_t index) const
{
    return Clock::now() - counters_.at(index).allocTime > kTimeoutThreshold;
}

} // namespace common
