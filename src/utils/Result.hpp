// src/utils/Result.hpp
// Lightweight result wrapper around std::optional<T>.
//
// Values are accessed exclusively through useOrFailed() — there is no
// get() because throwing exceptions is unsafe inside CAF actors.

#pragma once

#include "common/Macros.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace utils
{

template <typename T>
class Result
{
  public:
    Result() noexcept = default;

    explicit Result(T value) : data_(std::move(value)) {}

    /* implicit */ Result(std::nullopt_t) noexcept : data_(std::nullopt) {}

    [[nodiscard]] bool has_value() const noexcept
    {
        return data_.has_value();
    }

    template <typename FnValue, typename FnEmpty>
    [[nodiscard]] std::invoke_result_t<FnValue, T &> useOrFailed(FnValue &&onValue,
                                                                 FnEmpty &&onEmpty)
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
    [[nodiscard]] std::invoke_result_t<FnValue, const T &> useOrFailed(FnValue &&onValue,
                                                                       FnEmpty &&onEmpty) const
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

template <typename T>
class Result<T &>
{
  public:
    Result() noexcept = default;

    explicit Result(T &ref) noexcept : ptr_(&ref) {}

    /* implicit */ Result(std::nullopt_t) noexcept {}

    [[nodiscard]] bool has_value() const noexcept
    {
        return ptr_ != nullptr;
    }

    template <typename FnValue, typename FnEmpty>
    [[nodiscard]] std::invoke_result_t<FnValue, T &> useOrFailed(FnValue &&onValue,
                                                                 FnEmpty &&onEmpty)
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
    [[nodiscard]] std::invoke_result_t<FnValue, const T &> useOrFailed(FnValue &&onValue,
                                                                       FnEmpty &&onEmpty) const
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
