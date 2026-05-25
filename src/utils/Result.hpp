// src/utils/Result.hpp
// Lightweight result wrapper around std::optional<T>.
//
// Values are accessed exclusively through useOrFailed() — there is no
// get() because throwing exceptions is unsafe inside CAF actors.

#pragma once

#include "common/Macros.hpp"

#include <optional>
#include <utility>

namespace utils
{

template <typename T>
class Result
{
  public:
    // ----- construction ----------------------------------------------
    Result() noexcept = default;

    explicit Result(T value) : data_(std::move(value)) {}

    /* implicit */ Result(std::nullopt_t) noexcept : data_(std::nullopt) {}

    // ----- query -----------------------------------------------------
    [[nodiscard]] bool has_value() const noexcept
    {
        return data_.has_value();
    }

    // ----- branching -------------------------------------------------
    // 有值时调用 onValue，无值时调用 onEmpty。
    // 两个 lambda 的返回值类型必须一致。
    template <typename FnValue, typename FnEmpty>
    [[nodiscard]] auto useOrFailed(FnValue &&onValue, FnEmpty &&onEmpty)
        -> decltype(onValue(std::declval<T &>()))
    {
        if (FH_LIKELY(data_))
        {
            return onValue(*data_);
        }
        else
        {
            return onEmpty();
        }
    }

    template <typename FnValue, typename FnEmpty>
    [[nodiscard]] auto useOrFailed(FnValue &&onValue, FnEmpty &&onEmpty) const
        -> decltype(onValue(std::declval<const T &>()))
    {
        if (FH_LIKELY(data_))
        {
            return onValue(*data_);
        }
        else
        {
            return onEmpty();
        }
    }

  private:
    std::optional<T> data_;
};

// ---- Result<T&> 偏特化：引用语义，内部用指针 ---------------
template <typename T>
class Result<T &>
{
  public:
    // ----- construction ----------------------------------------------
    Result() noexcept = default;

    explicit Result(T &ref) noexcept : ptr_(&ref) {}

    /* implicit */ Result(std::nullopt_t) noexcept {}

    // ----- query -----------------------------------------------------
    [[nodiscard]] bool has_value() const noexcept
    {
        return ptr_ != nullptr;
    }

    // ----- branching -------------------------------------------------
    template <typename FnValue, typename FnEmpty>
    [[nodiscard]] auto useOrFailed(FnValue &&onValue, FnEmpty &&onEmpty)
        -> decltype(onValue(std::declval<T &>()))
    {
        if (FH_LIKELY(ptr_))
        {
            return onValue(*ptr_);
        }
        else
        {
            return onEmpty();
        }
    }

    template <typename FnValue, typename FnEmpty>
    [[nodiscard]] auto useOrFailed(FnValue &&onValue, FnEmpty &&onEmpty) const
        -> decltype(onValue(std::declval<const T &>()))
    {
        if (FH_LIKELY(ptr_))
        {
            return onValue(*ptr_);
        }
        else
        {
            return onEmpty();
        }
    }

  private:
    T *ptr_ = nullptr;
};

} // namespace utils
