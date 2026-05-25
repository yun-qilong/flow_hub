// src/utils/StaticPool.hpp
// Fixed-capacity memory pool backed by std::array.
//
// Elements are never moved — pointers remain valid for the pool's lifetime.
// Used as the backing store for custom static containers.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace utils
{

template <typename T, size_t Capacity>
class StaticPool
{
    struct Node
    {
        T *elem;
        Node *next;
        Node *prev;
    };

  public:
    StaticPool() noexcept
    {
        for (size_t i = 0; i < Capacity; ++i)
        {
            nodes_[i].elem = &slots_[i];
            nodes_[i].next = (i + 1 < Capacity) ? &nodes_[i + 1] : nullptr;
            nodes_[i].prev = (i > 0) ? &nodes_[i - 1] : nullptr;
        }
        unused_ = &nodes_[0];
    }

    StaticPool(const StaticPool &) = delete;
    StaticPool &operator=(const StaticPool &) = delete;
    StaticPool(StaticPool &&) = delete;
    StaticPool &operator=(StaticPool &&) = delete;

    [[nodiscard]] T *allocate() noexcept
    {
        if (not unused_)
        {
            return nullptr;
        }
        Node *node = unused_;
        unused_ = node->next;
        if (unused_)
        {
            unused_->prev = nullptr;
        }
        node->next = used_;
        node->prev = nullptr;
        if (used_)
        {
            used_->prev = node;
        }
        used_ = node;
        ++usedCount_;
        return node->elem;
    }

    void deallocate(T *ptr) noexcept
    {
        if (not ptr)
        {
            return;
        }
        Node *node = findNode(ptr);
        if (not node)
        {
            return;
        }
        unlink(node);
        node->next = unused_;
        node->prev = nullptr;
        if (unused_)
        {
            unused_->prev = node;
        }
        unused_ = node;
        --usedCount_;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return usedCount_;
    }
    [[nodiscard]] size_t available() const noexcept
    {
        return Capacity - usedCount_;
    }
    [[nodiscard]] static constexpr size_t capacity() noexcept
    {
        return Capacity;
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return usedCount_ == 0;
    }
    [[nodiscard]] bool full() const noexcept
    {
        return usedCount_ == Capacity;
    }

  private:
    Node *findNode(T *ptr) const noexcept
    {
        auto idx = static_cast<size_t>(ptr - slots_.data());
        if (idx >= Capacity)
        {
            return nullptr;
        }

        return const_cast<Node *>(&nodes_[idx]);
    }

    void unlink(Node *node) noexcept
    {
        if (node->prev)
        {
            node->prev->next = node->next;
        }
        else
        {
            used_ = node->next;
        }

        if (node->next)
        {
            node->next->prev = node->prev;
        }
    }

    std::array<T, Capacity> slots_{};
    std::array<Node, Capacity> nodes_{};
    Node *unused_ = nullptr;
    Node *used_ = nullptr;
    size_t usedCount_ = 0;
};

} // namespace utils
