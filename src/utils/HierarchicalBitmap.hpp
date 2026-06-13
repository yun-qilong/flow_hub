// src/utils/HierarchicalBitmap.hpp
// Two-level bitmap for BitCount ∈ [1, 4096].
//
// Level 1: StaticBitMap<kBlockCount> — bit=1 means the corresponding L2 block
//           is full (no free slots).
// Level 2: std::tuple of StaticBitMap<blockSize, uint64_t> — one per block.
//           All blocks use uint64_t storage, but each block has its own
//           compile-time kFullMask (64 for full blocks, kLastBlockBits for
//           the last block).  No phantom-bit hack.
//
// allocate() is O(1):
//   1. L1.firstFree()  →  first non-full block
//   2. L2[block].allocate()  →  bit index inside that block
//   3. If block became full  →  L1.markUsed(block)

#pragma once

#include "utils/StaticBitMap.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

namespace utils
{

namespace detail
{

template <size_t I, size_t TotalBits, size_t BlockBits = 64>
struct BlockSizeAt
{
    static constexpr size_t kFullBlocks = TotalBits / BlockBits;
    static constexpr size_t kLastBits =
        (TotalBits % BlockBits == 0) ? BlockBits : (TotalBits % BlockBits);
    static constexpr size_t value = (I < kFullBlocks) ? BlockBits : kLastBits;
};

} // namespace detail

template <size_t BitCount, typename Seq = std::make_index_sequence<(BitCount + 63) / 64>>
class HierarchicalBitmap;

template <size_t BitCount, size_t... Is>
class HierarchicalBitmap<BitCount, std::index_sequence<Is...>>
{
    static constexpr size_t kBlockBits = 64;
    static constexpr size_t kBlockCount = sizeof...(Is);

    static_assert(BitCount > 0 and BitCount <= kBlockBits * kBlockBits,
                  "HierarchicalBitmap: BitCount must be 1..4096");

    using BlockTuple =
        std::tuple<StaticBitMap<detail::BlockSizeAt<Is, BitCount>::value, uint64_t>...>;

    StaticBitMap<kBlockCount> level1_;
    BlockTuple level2_;

    static constexpr size_t getBlock(size_t idx)
    {
        return idx / kBlockBits;
    }
    static constexpr size_t getBit(size_t idx)
    {
        return idx % kBlockBits;
    }

    template <size_t I>
    int allocateImpl(size_t block)
    {
        if constexpr (I >= kBlockCount)
        {
            return -1;
        }
        else if (block == I)
        {
            return std::get<I>(level2_).allocate();
        }
        else
        {
            return allocateImpl<I + 1>(block);
        }
    }

    template <size_t I>
    void deallocateImpl(size_t block, int bit)
    {
        if constexpr (I >= kBlockCount)
        {
            return;
        }
        else if (block == I)
        {
            std::get<I>(level2_).deallocate(bit);
        }
        else
        {
            deallocateImpl<I + 1>(block, bit);
        }
    }

    template <size_t I>
    bool allUsedImpl(size_t block) const
    {
        if constexpr (I >= kBlockCount)
        {
            return false;
        }
        else if (block == I)
        {
            return std::get<I>(level2_).allUsed();
        }
        else
        {
            return allUsedImpl<I + 1>(block);
        }
    }

    template <size_t I>
    bool isUsedImpl(size_t block, int bit) const
    {
        if constexpr (I >= kBlockCount)
        {
            return false;
        }
        else if (block == I)
        {
            return std::get<I>(level2_).isUsed(bit);
        }
        else
        {
            return isUsedImpl<I + 1>(block, bit);
        }
    }

  public:
    HierarchicalBitmap() noexcept = default;
    ~HierarchicalBitmap() = default;

    HierarchicalBitmap(const HierarchicalBitmap &) = delete;
    HierarchicalBitmap &operator=(const HierarchicalBitmap &) = delete;
    HierarchicalBitmap(HierarchicalBitmap &&) = delete;
    HierarchicalBitmap &operator=(HierarchicalBitmap &&) = delete;

    [[nodiscard]] int allocate() noexcept
    {
        int block = level1_.firstFree();
        if (block < 0)
        {
            return -1;
        }
        size_t blk = static_cast<size_t>(block);

        int bit = allocateImpl<0>(blk);

        if (allUsedImpl<0>(blk))
        {
            level1_.markUsed(block);
        }
        return block * static_cast<int>(kBlockBits) + bit;
    }

    void deallocate(size_t idx) noexcept
    {
        size_t block = getBlock(idx);
        size_t bit = getBit(idx);

        bool isFull = allUsedImpl<0>(block);
        deallocateImpl<0>(block, static_cast<int>(bit));
        if (isFull)
        {
            level1_.deallocate(static_cast<int>(block));
        }
    }

    [[nodiscard]] bool isUsed(size_t idx) const noexcept
    {
        return isUsedImpl<0>(getBlock(idx), static_cast<int>(getBit(idx)));
    }

    [[nodiscard]] int countFree() const noexcept
    {
        return static_cast<int>(BitCount) - countUsed();
    }

    [[nodiscard]] int countUsed() const noexcept
    {
        return std::apply([](const auto &...blocks) { return (blocks.countUsed() + ...); },
                          level2_);
    }

    [[nodiscard]] bool allUsed() const noexcept
    {
        return level1_.allUsed();
    }

    void clear() noexcept
    {
        level1_.clear();
        std::apply([](auto &...blocks) { (blocks.clear(), ...); }, level2_);
    }

    [[nodiscard]] size_t blockCount() const noexcept
    {
        return kBlockCount;
    }
    [[nodiscard]] size_t capacity() const noexcept
    {
        return BitCount;
    }
};

} // namespace utils
