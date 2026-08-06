#pragma once

#include "fw/EoTypes.hpp"

#include <utility>
#include <vector>

namespace fw
{

// 消息序列化 round-trip 验证：serialize → deserialize → serialize，
// 断言两次序列化字节一致。用于检测 CAF inspect 缺失 / 字段映射错误。
// 需要 actor_system 上下文（EoAddress 字段序列化依赖 system registry）。
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
