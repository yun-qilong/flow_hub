// src/CPlane/SessionMgr.hpp
// Data-plane session layer — Session Manager (GTID lifecycle)
//
// 负责 GTID 的分配与回收，作为会话生命周期的管控入口。

#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"
#include "generated/Types.hpp"

#include <cstdint>

namespace common
{
class TaskPool;
}

namespace CPlane
{

using TempConfig = common::message::TempConfig;
using TaskCreateReq = common::message::TaskCreateReq;
using TaskCreateResp = common::message::TaskCreateResp;
using TaskDeleteReq = common::message::TaskDeleteReq;
using TaskDeleteResp = common::message::TaskDeleteResp;

class SessionMgr : public fw::EoBase<SessionMgr>
{
  public:
    explicit SessionMgr(fw::EoConfig &cfg, common::TaskPool &pool, fw::EoAddress accessGatewayAddr);

    void handle(const TaskCreateReq &req);
    void handle(const TaskDeleteReq &req);
    void handle(const TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<TempConfig>();
        onMsg<TaskCreateReq>();
        onMsg<TaskDeleteReq>();
    }

  private:
    void processCreateTask(common::TaskType taskType, TaskCreateResp &resp);
    void processDeleteTask(common::GTID gtid, TaskDeleteResp &resp);

    common::TaskPool &pool_;
    fw::EoAddress accessGatewayAddr_;
    fw::EoAddress businessMgrAddr_;
};

} // namespace CPlane
