// src/CPlane/SessionMgr.cpp

#include "CPlane/SessionMgr.hpp"
#include "common/TaskPool.hpp"
#include "common/UidUtil.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

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
    std::cout << "[SessionMgr] UserRegisterReq: username=" << req.username << "\n";

    UserRegisterResp resp;
    resp.head = req.head;
    resp.username = req.username;
    resp.connectionId = req.connectionId;

    if (req.username.size() > common::kMaxUsernameLen)
    {
        std::cerr << "[SessionMgr] register failed: username too long (max "
                  << +common::kMaxUsernameLen << ")\n";
        sendRegisterResp(resp, false);
        return;
    }

    if (usernameToId_.count(req.username) != 0)
    {
        std::cerr << "[SessionMgr] register failed: username already exists\n";
        sendRegisterResp(resp, false);
        return;
    }

    auto userId = allocateUserId();
    if (userId == common::kInvalidUserId)
    {
        std::cerr << "[SessionMgr] register failed: no free userId\n";
        sendRegisterResp(resp, false);
        return;
    }

    auto appType = req.head.appType;
    usernameToId_[req.username] = userId;
    auto uid = makeUid(userId, appType);
    std::cout << "[SessionMgr] registered: userId=" << static_cast<int>(userId) << " uid=0x"
              << std::hex << uid << std::dec << "\n";
    sendRegisterResp(resp, true);

    UserRegisterSessionReq registerReq;
    registerReq.head = req.head;
    registerReq.userId = userId;
    sendTo(sessionDataAddr_, std::move(registerReq));
}

void SessionMgr::handle(const UserLogoutReq &req)
{
    std::cout << "[SessionMgr] UserLogoutReq: uid=0x" << std::hex << req.head.uid << std::dec
              << "\n";

    UserLogoutResp resp;
    resp.head = req.head;

    this->requestThen(
        sessionDataAddr_, std::chrono::seconds(1), UserLogoutSessionReq{req.head},
        [this, resp = std::move(resp)](const UserLogoutSessionResp &sessionResp) mutable
        {
            resp.success = true;
            std::cout << "[SessionMgr] logout done, activeAdapterCount="
                      << static_cast<int>(sessionResp.activeAdapterCount) << "\n";
            sendTo(accessGatewayAddr_, std::move(resp));
        },
        [this](caf::error &err)
        { std::cerr << "[SessionMgr] logout failed: " << to_string(err) << "\n"; });
}

void SessionMgr::handle(const UserLoginReq &req)
{
    std::cout << "[SessionMgr] UserLoginReq: username=" << req.username << "\n";

    UserLoginResp resp;
    resp.head = req.head;
    resp.username = req.username;
    resp.connectionId = req.connectionId;

    auto it = usernameToId_.find(req.username);
    if (it == usernameToId_.end())
    {
        std::cerr << "[SessionMgr] login failed: username not found\n";
        sendLoginResp(resp, common::kInvalidUid, false, false, {});
        return;
    }

    auto userId = it->second;
    auto appType = req.head.appType;
    auto uid = makeUid(userId, appType);

    auto gtids = userRecords_.at(userId).at(static_cast<size_t>(appType)).gtids;

    std::cout << "[SessionMgr] sending UserLoginSessionReq to SessionData, gtids=" << gtids.size()
              << "\n";

    this->requestThen(
        sessionDataAddr_, std::chrono::seconds(1),
        std::move(buildLoginSessionReq(req.head, uid, gtids)),
        [this, resp = std::move(resp), gtids = gtids,
         uid](const UserLoginSessionResp &sessionResp) mutable
        { sendLoginResp(resp, uid, true, sessionResp.needWaitForData, gtids); },
        [this](caf::error &err)
        { std::cerr << "[SessionMgr] login request failed: " << to_string(err) << "\n"; });
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
    std::cout << "[SessionMgr] UserDeleteReq: uid=0x" << std::hex << uid << std::dec << "\n";

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
    std::cout << "[SessionMgr] deleted: username=" << username
              << " userId=" << static_cast<int>(userId) << "\n";
    sendTo(accessGatewayAddr_, std::move(resp));
}

void SessionMgr::handle(const TaskCreateReq &req)
{
    auto userId = common::getUserId(req.head.uid);
    auto appType = common::getAppType(req.head.uid);
    std::cout << "[SessionMgr] TaskCreateReq: userId=" << static_cast<int>(userId) << " taskType=0x"
              << std::hex << static_cast<uint16_t>(req.taskType) << std::dec << "\n";

    TaskCreateResp resp;
    resp.head = req.head;

    if (not isTaskTypeAllowed(appType, req.taskType))
    {
        std::cerr << "[SessionMgr] TaskCreate failed: taskType not allowed for this appType\n";
        resp.success = false;
        sendTo(accessGatewayAddr_, std::move(resp));
        return;
    }

    auto &record = userRecords_.at(userId).at(static_cast<size_t>(appType));
    pool_.allocate(req.taskType)
        .useOrFailed(
            [&](common::GTID &gtid)
            {
                record.gtids.push_back(gtid);
                resp.head.gtidList = {gtid};
                resp.success = true;
                std::cout << "[SessionMgr] TaskCreate success: gtid=0x" << std::hex << gtid
                          << std::dec << "\n";
                sendTo(accessGatewayAddr_, std::move(resp));
            },
            [&]()
            {
                std::cerr << "[SessionMgr] TaskCreate failed: no available GTID\n";
                resp.success = false;
                sendTo(accessGatewayAddr_, std::move(resp));
            });
}

void SessionMgr::handle(const TaskDeleteReq &req)
{
    auto gtid = req.head.gtidList.empty() ? common::kInvalidGtid : req.head.gtidList[0];
    auto userId = common::getUserId(req.head.uid);
    auto appType = common::getAppType(req.head.uid);
    std::cout << "[SessionMgr] TaskDeleteReq: gtid=0x" << std::hex << gtid << std::dec
              << " userId=" << static_cast<int>(userId) << "\n";

    TaskDeleteResp resp;
    resp.head = req.head;

    if (not eraseGtid(userId, appType, gtid))
    {
        std::cerr << "[SessionMgr] TaskDelete failed: gtid not found in user record\n";
        resp.success = false;
        sendTo(accessGatewayAddr_, std::move(resp));
        return;
    }

    pool_.deallocate(gtid);
    sendTo(sessionDataAddr_, TaskDeleteSessionReq{req.head});

    resp.success = true;
    std::cout << "[SessionMgr] TaskDelete success: gtid=0x" << std::hex << gtid << std::dec << "\n";
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
            return name;
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
