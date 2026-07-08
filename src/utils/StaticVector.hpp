#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "utils/Result.hpp"

namespace utils
{

template <typename T, size_t N>
class StaticVector
{
  public:
    using value_type = T;
    using size_type = size_t;
    using iterator = T *;
    using const_iterator = const T *;

    void push_back(const T &value)
    {
        data_.at(size_) = value;
        ++size_;
    }

    void clear()
    {
        size_ = 0;
    }

    size_t size() const
    {
        return size_;
    }

    bool empty() const
    {
        return size_ == 0;
    }

    T &operator[](size_t i)
    {
        return data_.at(i);
    }

    const T &operator[](size_t i) const
    {
        return data_.at(i);
    }

    Result<T &> at(size_t i)
    {
        if (i >= size_)
        {
            return Result<T &>{};
        }
        return Result<T &>{data_[i]};
    }

    Result<const T &> at(size_t i) const
    {
        if (i >= size_)
        {
            return Result<const T &>{};
        }
        return Result<const T &>{data_[i]};
    }

    iterator erase(iterator pos)
    {
        auto last = end();
        for (auto it = pos; it + 1 != last; ++it)
        {
            *it = *(it + 1);
        }
        --size_;
        return pos;
    }

    T *begin()
    {
        return data_.data();
    }

    const T *begin() const
    {
        return data_.data();
    }

    T *end()
    {
        return data_.data() + size_;
    }

    const T *end() const
    {
        return data_.data() + size_;
    }

  private:
    std::array<T, N> data_{};
    size_t size_ = 0;
};

} // namespace utils
