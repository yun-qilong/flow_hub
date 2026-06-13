// src/common/TaskPool.hpp
// Per-TaskType fixed-capacity GTID allocator and Context store.
//
// GTID layout (16-bit):
//   [15:12] Category   (0x0=System, 0x7=User, 0xC=Other)
//   [11:6]  TaskType   (0..63, 64 types per category)
//   [5:0]   Index      (0..63, 64 concurrent tasks per type)
//
// Context lookup is O(1):  idx = gtid & 0x3F, then index into the
// corresponding std::array.

#pragma once

#include "ContextManager.hpp"
#include "generated/context/AiChatContext.hpp"
#include "generated/TaskType.hpp"
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

  public:
    explicit TaskPool(size_t capacityPerType = kIndexCount);

    // ---- GTID lifecycle ----------------------------------------------
    utils::Result<GTID> allocate(TaskType type);
    void deallocate(GTID gtid);

    // ---- Context access (hot path) -----------------------------------
    // 可读写：GTID 类型必须与 Ctx 严格一致，否则打日志返空
    template <typename Ctx>
    utils::Result<Ctx &> getContext(GTID gtid);

    // 只读：仅校验 context 有效性，不做类型检查
    template <typename Ctx>
    utils::Result<const Ctx &> getContextRead(GTID gtid);

    // ---- helpers -----------------------------------------------------
    size_t available(TaskType type) const;

  private:
    static constexpr TaskType getType(GTID gtid);
    static constexpr uint8_t getIndex(GTID gtid);

    ContextManager<context::AiChatContext, kIndexCount> aiChatContexts_;
};

} // namespace common
