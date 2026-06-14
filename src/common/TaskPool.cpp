// src/common/TaskPool.cpp

#include "TaskPool.hpp"

namespace common
{

utils::Result<GTID> TaskPool::allocate(TaskType type)
{
    int idx = detail::allocateInTuple(type, managers_);
    if (idx < 0)
    {
        return std::nullopt;
    }
    return utils::Result<GTID>(makeGtid(type, static_cast<uint8_t>(idx)));
}

void TaskPool::deallocate(GTID gtid)
{
    TaskType type = extractTaskType(gtid);
    uint8_t idx = extractIndex(gtid);
    detail::deallocateInTuple(type, managers_, idx);
}

size_t TaskPool::available(TaskType type) const
{
    return detail::availableInTuple(type, managers_);
}

} // namespace common
