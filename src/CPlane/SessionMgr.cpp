#include "CPlane/SessionMgr.hpp"
#include "common/TaskPool.hpp"
#include "utils/SysLog.hpp"

namespace CPlane
{

SessionMgr::SessionMgr(fw::EoConfig &cfg, common::TaskPool &pool, fw::EoAddress accessGatewayAddr)
    : fw::EoBase<SessionMgr>(cfg), pool_(pool), accessGatewayAddr_(std::move(accessGatewayAddr))
{
    sendTo(accessGatewayAddr_, TempConfig{1});
}

void SessionMgr::handle(const TempConfig &msg)
{
    if (msg.tag == 4)
    {
        businessMgrAddr_ = senderAddress();
    }
}

void SessionMgr::handle(const TaskCreateReq &req)
{
    LG_INFO("[SessionMgr] TaskCreateReq: taskType=0x%x", static_cast<uint16_t>(req.taskType));

    TaskCreateResp resp;
    resp.head = req.head;
    resp.cookie = req.cookie;
    processCreateTask(req.taskType, resp);
}

void SessionMgr::processCreateTask(common::TaskType taskType, TaskCreateResp &resp)
{
    pool_.allocate(taskType).useOrFailed(
        [&](common::GTID &gtid)
        {
            resp.head.sessionTaskId = gtid;
            resp.isSuccess = true;
            LG_INFO("[SessionMgr] TaskCreate success: gtid=0x%x", gtid);
            sendTo(accessGatewayAddr_, std::move(resp));
        },
        [&]()
        {
            LG_ERR("[SessionMgr] TaskCreate failed: no available GTID");
            resp.isSuccess = false;
            sendTo(accessGatewayAddr_, std::move(resp));
        });
}

void SessionMgr::handle(const TaskDeleteReq &req)
{
    auto gtid = req.head.sessionTaskId;
    LG_INFO("[SessionMgr] TaskDeleteReq: gtid=0x%x", gtid);

    TaskDeleteResp resp;
    resp.head = req.head;
    processDeleteTask(gtid, resp);
}

void SessionMgr::processDeleteTask(common::GTID gtid, TaskDeleteResp &resp)
{
    if (gtid == common::kInvalidGtid)
    {
        LG_ERR("[SessionMgr] TaskDelete failed: invalid gtid");
        resp.isSuccess = false;
        sendTo(accessGatewayAddr_, std::move(resp));
        return;
    }

    pool_.deallocate(gtid);
    resp.isSuccess = true;
    LG_INFO("[SessionMgr] TaskDelete success: gtid=0x%x", gtid);
    sendTo(accessGatewayAddr_, std::move(resp));
}

void SessionMgr::handle(const BusTaskCreateReq &req)
{
    LG_INFO("[SessionMgr] BusTaskCreateReq: sessionTaskId=0x%x types=%zu", req.head.sessionTaskId,
            req.taskTypes.size());

    BusTaskCreateResp resp;
    resp.head = req.head;
    processCreateBusTasks(req.taskTypes, resp);
    replyToSender(std::move(resp));
}

void SessionMgr::processCreateBusTasks(const std::vector<common::TaskType> &taskTypes,
                                       BusTaskCreateResp &resp)
{
    std::vector<common::GTID> allocated;
    allocated.reserve(taskTypes.size());

    for (auto type : taskTypes)
    {
        auto gtid = pool_.allocate(type).useOrFailed([](common::GTID &g) { return g; },
                                                     []() { return common::kInvalidGtid; });
        if (gtid == common::kInvalidGtid)
        {
            recycleBusTasks(allocated);
            resp.isSuccess = false;
            LG_WRN("[SessionMgr] BusTaskCreate failed: no available GTID for type=0x%x",
                   static_cast<uint16_t>(type));
            return;
        }
        allocated.push_back(gtid);
    }

    resp.head.busTaskIds = std::move(allocated);
    resp.isSuccess = true;
    LG_INFO("[SessionMgr] BusTaskCreate success: %zu GTID(s) allocated",
            resp.head.busTaskIds.size());
}

void SessionMgr::recycleBusTasks(const std::vector<common::GTID> &gtids)
{
    for (auto gtid : gtids)
    {
        if (gtid == common::kInvalidGtid)
        {
            continue;
        }
        pool_.deallocate(gtid);
    }
}

void SessionMgr::handle(const BusTaskDeleteReq &req)
{
    LG_INFO("[SessionMgr] BusTaskDeleteReq: sessionTaskId=0x%x count=%zu", req.head.sessionTaskId,
            req.head.busTaskIds.size());

    BusTaskDeleteResp resp;
    resp.head = req.head;
    processDeleteBusTasks(req.head.busTaskIds, resp);
    replyToSender(std::move(resp));
}

void SessionMgr::processDeleteBusTasks(const std::vector<common::GTID> &gtids,
                                       BusTaskDeleteResp &resp)
{
    recycleBusTasks(gtids);
    resp.isSuccess = true;
    LG_INFO("[SessionMgr] BusTaskDelete success: %zu GTID(s) recycled", gtids.size());
}

} // namespace CPlane
