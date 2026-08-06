#pragma once

#include <utility>

namespace utils
{

template <typename FnOk, typename FnFail>
auto tryOrFailed(FnOk &&ok, FnFail &&fail)
{
    try
    {
        return ok();
    }
    catch (...)
    {
        return fail();
    }
}

} // namespace utils
