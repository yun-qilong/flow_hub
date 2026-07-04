// Auto-generated from structs.mt — DO NOT EDIT

#pragma once

#include "caf/all.hpp"

namespace utils
{
template <typename T, size_t N>
class StaticVector;
}

namespace utils
{

template <typename T, size_t N, class Inspector>
bool inspect(Inspector &f, StaticVector<T, N> &x)
{
    auto sz = x.size();
    if (not f.begin_sequence(sz))
    {
        return false;
    }
    for (size_t i = 0; i < sz; ++i)
    {
        if (not f.apply(x[i]))
        {
            return false;
        }
    }
    return f.end_sequence();
}

} // namespace utils
