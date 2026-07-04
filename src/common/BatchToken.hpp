#pragma once

#include <cstddef>
#include <cstdint>

namespace common
{

struct BatchToken
{
    size_t index;
    uint16_t epoch;
};

template <class Inspector>
bool inspect(Inspector &f, BatchToken &x)
{
    return f.object(x).fields(f.field("index", x.index), f.field("epoch", x.epoch));
}

} // namespace common
