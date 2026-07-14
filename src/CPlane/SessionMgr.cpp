// src/CPlane/SessionMgr.cpp

#include "CPlane/SessionMgr.hpp"
#include "common/TaskPool.hpp"
#include "common/UidUtil.hpp"
#include "utils/SysLog.hpp"

#include <algorithm>
#include <chrono>

namespace CPlane
{
using UserRegisterSessionReq = common::message::UserRegisterSessionReq;
using UserLoginSessionReq = common::message::UserLoginSessionReq;
using UserLogoutSessionReq = common::message::UserLogoutSessionReq;
using UserLogoutSessionResp = common::message::UserLogoutSessionResp;
using UserLoginSessionResp = common::message::UserLoginSessionResp;
using TaskCreateResp = common::message::TaskCreateResp;
using TaskDeleteResp = common::message::TaskDeleteResp;
using TaskDeleteSessionReq = common::message::TaskDeleteSessionReq;

SessionMgr::SessionMgr(fw::EoConfig &cfg, common::TaskPool &pool, fw::EoAddress accessGatewayAddr,
                       fw::EoAddress sessionDataAddr)
    : fw::EoBase<SessionMgr>(cfg), pool_(pool), accessGatewayAddr_(std::move(accessGatewayAddr)),
      sessionDataAddr_(std::move(sessionDataAddr))
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

void SessionMgr::handle(const UserRegisterReq &req)
{
    LG_INFO("[SessionMgr] UserRegisterReq: username=%s", req.username.c_str());

    UserRegisterResp resp;
    resp.head = req.head;
    resp.username = req.username;
    resp.connectionId = req.connectionId;

    if (not isUsernameValid(req.username))
    {
        sendRegisterResp(resp, false);
        return;
    }

    auto userId = allocateUserId();
    if (userId == common::kInvalidUserId)
    {
        LG_ERR("[SessionMgr] register failed: no free userId");
        sendRegisterResp(resp, false);
        return;
    }

    auto appType = req.head.appType;
    usernameToId_[req.username] = userId;
    auto uid = makeUid(userId, appType);
    LG_INFO("[SessionMgr] registered: userId=%d uid=0x%x", static_cast<int>(userId), uid);
    sendRegisterResp(resp, true);

    noticeSessionData(req.head, userId);
}

void SessionMgr::handle(const UserLogoutReq &req)
{
    LG_INFO("[SessionMgr] UserLogoutReq: uid=0x%x", req.head.uid);

    UserLogoutResp resp;
    resp.head = req.head;

    this->requestThen(
        sessionDataAddr_, std::chrono::seconds(1), UserLogoutSessionReq{req.head},
        [this, resp = std::move(resp)](const UserLogoutSessionResp &sessionResp) mutable
        {
            resp.success = true;
            LG_INFO("[SessionMgr] logout done, activeAdapterCount=%d",
                    static_cast<int>(sessionResp.activeAdapterCount));
            sendTo(accessGatewayAddr_, std::move(resp));
        },
        [this](caf::error &err)
        { LG_ERR("[SessionMgr] logout failed: %s", to_string(err).c_str()); });
}

void SessionMgr::handle(const UserLoginReq &req)
{
    LG_INFO("[SessionMgr] UserLoginReq: username=%s", req.username.c_str());

    UserLoginResp resp;
    resp.head = req.head;
    resp.username = req.username;
    resp.connectionId = req.connectionId;

    auto it = usernameToId_.find(req.username);
    if (it == usernameToId_.end())
    {
        LG_ERR("[SessionMgr] login failed: username not found");
        sendLoginResp(resp, common::kInvalidUid, false, false, {});
        return;
    }

    auto userId = it->second;
    auto appType = req.head.appType;
    auto uid = makeUid(userId, appType);

    auto gtids = userRecords_.at(userId).at(static_cast<size_t>(appType)).gtids;
    LG_DBG("[SessionMgr] sending UserLoginSessionReq to SessionData, gtids=%zu", gtids.size());
    this->requestThen(
        sessionDataAddr_, std::chrono::seconds(1),
        std::move(buildLoginSessionReq(req.head, uid, gtids)),
        [this, resp = std::move(resp), gtids = gtids,
         uid](const UserLoginSessionResp &sessionResp) mutable
        { sendLoginResp(resp, uid, true, sessionResp.needWaitForData, gtids); },
        [this](caf::error &err)
        { LG_ERR("[SessionMgr] login request failed: %s", to_string(err).c_str()); });
}

bool SessionMgr::isUsernameValid(const std::string &username) const
{
    if (username.size() > common::kMaxUsernameLen)
    {
        LG_ERR("[SessionMgr] register failed: username too long (max %u)", common::kMaxUsernameLen);
        return false;
    }

    if (usernameToId_.count(username) != 0)
    {
        LG_ERR("[SessionMgr] register failed: username already exists");
        return false;
    }

    return true;
}

void SessionMgr::noticeSessionData(const common::message::UserHead &head, common::UserId userId)
{
    UserRegisterSessionReq registerReq;
    registerReq.head = head;
    registerReq.userId = userId;
    sendTo(sessionDataAddr_, std::move(registerReq));
}

void SessionMgr::processCreateTask(common::UserId userId, common::AppType appType,
                                   common::TaskType taskType, TaskCreateResp &resp)
{
    auto &record = userRecords_.at(userId).at(static_cast<size_t>(appType));
    pool_.allocate(taskType).useOrFailed(
        [&](common::GTID &gtid)
        {
            record.gtids.push_back(gtid);
            resp.head.gtidList = {gtid};
            resp.success = true;
            LG_DBG("[SessionMgr] TaskCreate success: gtid=0x%x", gtid);
            sendTo(accessGatewayAddr_, std::move(resp));
        },
        [&]()
        {
            LG_ERR("[SessionMgr] TaskCreate failed: no available GTID");
            resp.success = false;
            sendTo(accessGatewayAddr_, std::move(resp));
        });
}

common::UserId SessionMgr::allocateUserId()
{
    auto freeMask = ~uidBitset_.to_ullong();
    if (freeMask == 0)
    {
        return common::kInvalidUserId;
    }
    auto userId = static_cast<common::UserId>(__builtin_ctzll(freeMask));
    uidBitset_.set(userId);
    return userId;
}

UserLoginSessionReq SessionMgr::buildLoginSessionReq(
    const common::message::UserHead &head, uint16_t uid,
    const utils::StaticVector<common::GTID, common::kMaxGtidsPerUser> &gtids)
{
    UserLoginSessionReq req;
    req.head = head;
    req.head.uid = uid;
    req.gtids = gtids;
    return req;
}

void SessionMgr::sendRegisterResp(UserRegisterResp &resp, bool success)
{
    resp.success = success;
    sendTo(accessGatewayAddr_, std::move(resp));
}

void SessionMgr::sendLoginResp(UserLoginResp &resp, uint16_t uid, bool success,
                               bool needWaitForData,
                               utils::StaticVector<common::GTID, common::kMaxGtidsPerUser> gtids)
{
    resp.head.uid = uid;
    resp.success = success;
    resp.needWaitForData = needWaitForData;
    resp.gtids = gtids;
    sendTo(accessGatewayAddr_, std::move(resp));
}

void SessionMgr::handle(const UserDeleteReq &req)
{
    auto uid = req.head.uid;
    auto userId = common::getUserId(uid);
    LG_INFO("[SessionMgr] UserDeleteReq: uid=0x%x", uid);

    UserDeleteResp resp;
    resp.head = req.head;

    for (size_t i = 0; i < common::kMaxAppTypes; ++i)
    {
        deallocateUserGtids(userId, static_cast<common::AppType>(i));
        userRecords_.at(userId).at(i).clear();
    }

    auto username = findUsernameByUserId(userId);
    usernameToId_.erase(username);
    releaseUserId(userId);

    resp.success = true;
    LG_INFO("[SessionMgr] deleted: username=%s userId=%d", username.c_str(),
            static_cast<int>(userId));
    sendTo(accessGatewayAddr_, std::move(resp));
}

void SessionMgr::handle(const TaskCreateReq &req)
{
    auto userId = common::getUserId(req.head.uid);
    auto appType = common::getAppType(req.head.uid);
    LG_DBG("[SessionMgr] TaskCreateReq: userId=%d taskType=0x%x", static_cast<int>(userId),
           static_cast<uint16_t>(req.taskType));

    TaskCreateResp resp;
    resp.head = req.head;

    if (not isTaskTypeAllowed(appType, req.taskType))
    {
        LG_ERR("[SessionMgr] TaskCreate failed: taskType not allowed for this appType");
        resp.success = false;
        sendTo(accessGatewayAddr_, std::move(resp));
        return;
    }

    processCreateTask(userId, appType, req.taskType, resp);
}

void SessionMgr::handle(const TaskDeleteReq &req)
{
    auto gtid = req.head.gtidList.empty() ? common::kInvalidGtid : req.head.gtidList[0];
    auto userId = common::getUserId(req.head.uid);
    auto appType = common::getAppType(req.head.uid);
    LG_DBG("[SessionMgr] TaskDeleteReq: gtid=0x%x userId=%d", gtid, static_cast<int>(userId));

    TaskDeleteResp resp;
    resp.head = req.head;

    if (not eraseGtid(userId, appType, gtid))
    {
        LG_ERR("[SessionMgr] TaskDelete failed: gtid not found in user record");
        resp.success = false;
        sendTo(accessGatewayAddr_, std::move(resp));
        return;
    }

    pool_.deallocate(gtid);
    sendTo(sessionDataAddr_, TaskDeleteSessionReq{req.head});

    resp.success = true;
    LG_DBG("[SessionMgr] TaskDelete success: gtid=0x%x", gtid);
    sendTo(accessGatewayAddr_, std::move(resp));
}

bool SessionMgr::eraseGtid(common::UserId userId, common::AppType appType, common::GTID gtid)
{
    auto &record = userRecords_.at(userId).at(static_cast<size_t>(appType));
    auto *it = std::find(record.gtids.begin(), record.gtids.end(), gtid);
    if (it != record.gtids.end())
    {
        record.gtids.erase(it);
        return true;
    }
    return false;
}

std::string SessionMgr::findUsernameByUserId(common::UserId userId) const
{
    for (const auto &[name, id] : usernameToId_)
    {
        if (id == userId)
        {
            return {name};
        }
    }
    return {};
}

void SessionMgr::deallocateUserGtids(common::UserId userId, common::AppType appType)
{
    auto &record = userRecords_.at(userId).at(static_cast<size_t>(appType));
    for (auto gtid : record.gtids)
    {
        pool_.deallocate(gtid);
    }
}

void SessionMgr::releaseUserId(common::UserId userId)
{
    uidBitset_.reset(userId);
}

} // namespace CPlane
