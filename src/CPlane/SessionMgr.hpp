#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"
#include "generated/Types.hpp"

#include <cstdint>
#include <vector>

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
using BusTaskCreateReq = common::message::BusTaskCreateReq;
using BusTaskCreateResp = common::message::BusTaskCreateResp;
using BusTaskDeleteReq = common::message::BusTaskDeleteReq;
using BusTaskDeleteResp = common::message::BusTaskDeleteResp;

class SessionMgr : public fw::EoBase<SessionMgr>
{
  public:
    explicit SessionMgr(fw::EoConfig &cfg, common::TaskPool &pool, fw::EoAddress accessGatewayAddr);

    void handle(const TaskCreateReq &req);
    void handle(const TaskDeleteReq &req);
    void handle(const BusTaskCreateReq &req);
    void handle(const BusTaskDeleteReq &req);
    void handle(const TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<TempConfig>();
        onMsg<TaskCreateReq>();
        onMsg<TaskDeleteReq>();
        onMsg<BusTaskCreateReq>();
        onMsg<BusTaskDeleteReq>();
    }

  private:
    void processCreateTask(common::TaskType taskType, TaskCreateResp &resp);
    void processDeleteTask(common::GTID gtid, TaskDeleteResp &resp);
    void processCreateBusTasks(const std::vector<common::TaskType> &taskTypes,
                               BusTaskCreateResp &resp);
    void processDeleteBusTasks(const std::vector<common::GTID> &gtids, BusTaskDeleteResp &resp);
    void recycleBusTasks(const std::vector<common::GTID> &gtids);

    common::TaskPool &pool_;
    fw::EoAddress accessGatewayAddr_;
    fw::EoAddress businessMgrAddr_;
};

} // namespace CPlane
