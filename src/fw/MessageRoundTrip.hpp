#pragma once

#include "fw/EoTypes.hpp"

#include <utility>
#include <vector>

namespace fw
{

template <typename M>
bool roundTripSerialize(caf::actor_system &sys, const M &src)
{
    caf::byte_buffer buf;
    caf::binary_serializer sink{sys, buf};
    if (not sink.apply(src))
    {
        return false;
    }

    M dst{};
    caf::binary_deserializer source{sys, buf};
    if (not source.apply(dst))
    {
        return false;
    }

    caf::byte_buffer buf2;
    caf::binary_serializer sink2{sys, buf2};
    if (not sink2.apply(dst))
    {
        return false;
    }

    return buf == buf2;
}

} // namespace fw
