// src/utils/StaticBitMap.hpp
// Compile-time sized bitmap backed by a uint of appropriate width.
//
//   StaticBitMap<BitCount>           → auto-deduce smallest fitting uint
//   StaticBitMap<BitCount, StorageT> → use StorageT explicitly (must fit)
//
// StorageT defaults to void (meaning "auto-deduce").  When provided
// explicitly, StorageT must be uint8_t / uint16_t / uint32_t / uint64_t.
//
// All bit operations use uint64_t promotion + GCC/Clang builtins,
// so the hot path is branch-free and inlined.

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace utils
{

template <size_t BitCount, typename StorageT = void>
class StaticBitMap
{
    static_assert(BitCount > 0, "BitCount must be > 0");

    static constexpr bool kStorageProvided = not std::is_same_v<StorageT, void>;

    using DeducedStorage = std::conditional_t<
        BitCount <= 8, uint8_t,
        std::conditional_t<BitCount <= 16, uint16_t,
                           std::conditional_t<BitCount <= 32, uint32_t, uint64_t>>>;

    using StorageType = std::conditional_t<kStorageProvided, StorageT, DeducedStorage>;

    // 编译期算最大位宽，避免 sizeof(void)
    static constexpr size_t kMaxBits = []() constexpr -> size_t
    {
        if constexpr (kStorageProvided)
        {
            return sizeof(StorageT) * 8;
        }
        else
        {
            return sizeof(DeducedStorage) * 8;
        }
    }();

    static_assert(not kStorageProvided or (BitCount <= kMaxBits),
                  "Provided StorageT is too small for BitCount");
    static_assert(BitCount <= kMaxBits, "BitCount exceeds maximum storage width (64 bits)");

    static constexpr StorageType kFullMask = []() constexpr
    {
        if constexpr (BitCount == sizeof(StorageType) * 8)
        {
            return ~StorageType(0);
        }
        else
        {
            return (StorageType(1) << BitCount) - 1;
        }
    }();

    StorageType mask_ = 0;

  public:
    StaticBitMap() noexcept = default;
    ~StaticBitMap() = default;

    StaticBitMap(const StaticBitMap &) = delete;
    StaticBitMap &operator=(const StaticBitMap &) = delete;
    StaticBitMap(StaticBitMap &&) = delete;
    StaticBitMap &operator=(StaticBitMap &&) = delete;

    [[nodiscard]] int allocate() noexcept
    {
        if (mask_ == kFullMask)
        {
            return -1;
        }

        uint64_t validPositions =
            (~static_cast<uint64_t>(mask_)) & static_cast<uint64_t>(kFullMask);
        int idx = __builtin_ctzll(validPositions);
        mask_ |= static_cast<StorageType>(StorageType(1) << idx);
        return idx;
    }

    void deallocate(int idx) noexcept
    {
        mask_ &= ~(static_cast<StorageType>(StorageType(1) << idx));
    }

    [[nodiscard]] bool isUsed(int idx) const noexcept
    {
        return (mask_ >> idx) & StorageType(1);
    }

    [[nodiscard]] int countFree() const noexcept
    {
        return static_cast<int>(BitCount) - __builtin_popcountll(static_cast<uint64_t>(mask_));
    }

    [[nodiscard]] int countUsed() const noexcept
    {
        return __builtin_popcountll(static_cast<uint64_t>(mask_));
    }

    [[nodiscard]] bool allUsed() const noexcept
    {
        return mask_ == kFullMask;
    }

    void markUsed(int idx) noexcept
    {
        mask_ |= static_cast<StorageType>(StorageType(1) << idx);
    }

    [[nodiscard]] int firstFree() const noexcept
    {
        if (mask_ == kFullMask)
        {
            return -1;
        }
        uint64_t freeBits = (~static_cast<uint64_t>(mask_)) & static_cast<uint64_t>(kFullMask);
        return __builtin_ctzll(freeBits);
    }

    void clear() noexcept
    {
        mask_ = 0;
    }

    [[nodiscard]] StorageType mask() const noexcept
    {
        return mask_;
    }
};

} // namespace utils
