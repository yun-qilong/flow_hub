// src/common/TaskPool.hpp
// Per-TaskType fixed-capacity GTID allocator and Context store.
//
// GTID layout (16-bit, ADR-0008):
//   [15:12] Category   (4 bits: 0x0=System, 0x7=User, 0xC=Other)
//   [11:6]  TaskType   (6 bits, sub-type within category, 0..63)
//   [5:0]   Index      (6 bits, 64 concurrent tasks per sub-type)
//
// TaskType enum encoding:
//   value = (Category << 6) | subType
// So GTID = (TaskType << 6) | index — TaskType bits directly mirror GTID [15:6].
//
// Storage: std::tuple<ContextManager<Ctx, 64>...> — one per TaskType.
// Dispatch: generated switch helpers in TaskPoolTypes.hpp.
// Context lookup is O(1):  extract (category, subType) from GTID,
// reconstruct TaskType, then index into tuple.

#pragma once

#include "generated/TaskPoolTypes.hpp"
#include "generated/Types.hpp"
#include "utils/Result.hpp"

#include <cstddef>
#include <cstdint>

namespace common
{

class TaskPool
{
    static constexpr size_t kIndexBits = 6;
    static constexpr size_t kIndexCount = 1 << kIndexBits; // 64
    static constexpr GTID kIndexMask = kIndexCount - 1;    // 0x3F
    static constexpr GTID kTaskTypeShift = kIndexBits;     // 6

  public:
    TaskPool() = default;

    utils::Result<GTID> allocate(TaskType type);
    void deallocate(GTID gtid);

    template <typename Ctx>
    utils::Result<Ctx &> getContext(GTID gtid);

    template <typename Ctx>
    utils::Result<const Ctx &> getContextRead(GTID gtid) const;

    size_t available(TaskType type) const;

  private:
    static constexpr TaskType extractTaskType(GTID gtid)
    {
        return static_cast<TaskType>(gtid >> kTaskTypeShift);
    }

    static constexpr uint8_t extractIndex(GTID gtid)
    {
        return static_cast<uint8_t>(gtid & kIndexMask);
    }

    static constexpr GTID makeGtid(TaskType type, uint8_t index)
    {
        return static_cast<GTID>((static_cast<GTID>(type) << kTaskTypeShift) | index);
    }

    ContextManagerTuple managers_;
};

template <typename Ctx>
utils::Result<Ctx &> TaskPool::getContext(GTID gtid)
{
    constexpr TaskType expectedType = ContextToTaskType<Ctx>::kValue;
    TaskType gtidType = extractTaskType(gtid);

    if (FH_LIKELY(gtidType == expectedType))
    {
        uint8_t idx = extractIndex(gtid);
        return utils::Result<Ctx &>(std::get<ContextManager<Ctx, kIndexCount>>(managers_).get(idx));
    }
    return std::nullopt;
}

template <typename Ctx>
utils::Result<const Ctx &> TaskPool::getContextRead(GTID gtid) const
{
    constexpr TaskType expectedType = ContextToTaskType<Ctx>::kValue;
    TaskType gtidType = extractTaskType(gtid);

    if (FH_LIKELY(gtidType == expectedType))
    {
        uint8_t idx = extractIndex(gtid);
        return utils::Result<const Ctx &>(
            std::get<ContextManager<Ctx, kIndexCount>>(managers_).getRead(idx));
    }
    return std::nullopt;
}

} // namespace common
