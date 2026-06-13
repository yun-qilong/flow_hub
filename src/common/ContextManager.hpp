// src/common/ContextManager.hpp
// Fixed-capacity Context store with integrated StaticBitMap slot management.
//
//   ContextManager<Ctx, Capacity>
//
// Internal:
//   - std::array<Ctx, Capacity>  — context storage (default-constructed)
//   - StaticBitMap<Capacity>     — slot occupancy tracking
//
// This class co-locates the context array and its bitmap to prevent the
// two from drifting apart.  TaskPool composes one ContextManager per
// TaskType.

#pragma once

#include "utils/StaticBitMap.hpp"

#include <array>
#include <cstddef>

namespace common
{

using utils::StaticBitMap;

template <typename Ctx, size_t Capacity>
class ContextManager
{
  public:
    ContextManager() noexcept = default;
    ~ContextManager() = default;

    ContextManager(const ContextManager &) = delete;
    ContextManager &operator=(const ContextManager &) = delete;
    ContextManager(ContextManager &&) = delete;
    ContextManager &operator=(ContextManager &&) = delete;

    [[nodiscard]] int allocate() noexcept;

    // 回收 slot
    void deallocate(int idx) noexcept;

    [[nodiscard]] Ctx &get(int idx) noexcept;
    [[nodiscard]] const Ctx &getRead(int idx) const noexcept;

  private:
    StaticBitMap<Capacity> bitmap_;
    std::array<Ctx, Capacity> contexts_;
};

} // namespace common

namespace common
{

template <typename Ctx, size_t Capacity>
[[nodiscard]] int ContextManager<Ctx, Capacity>::allocate() noexcept
{
    int idx = bitmap_.allocate();
    if (idx >= 0)
    {
        contexts_[static_cast<size_t>(idx)].clear();
    }
    return idx;
}

template <typename Ctx, size_t Capacity>
void ContextManager<Ctx, Capacity>::deallocate(int idx) noexcept
{
    bitmap_.deallocate(idx);
}

template <typename Ctx, size_t Capacity>
[[nodiscard]] Ctx &ContextManager<Ctx, Capacity>::get(int idx) noexcept
{
    return contexts_[static_cast<size_t>(idx)];
}

template <typename Ctx, size_t Capacity>
[[nodiscard]] const Ctx &ContextManager<Ctx, Capacity>::getRead(int idx) const noexcept
{
    return contexts_[static_cast<size_t>(idx)];
}

} // namespace common
