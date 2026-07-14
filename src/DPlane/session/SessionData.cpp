// src/DPlane/session/SessionData.cpp

#include "DPlane/session/SessionData.hpp"

#include "common/UidUtil.hpp"

namespace DPlane::session
{

using AiChatBusinessReq = common::message::AiChatBusinessReq;
using AiChatBusinessResp = common::message::AiChatBusinessResp;
using AiChatMsgAck = common::message::AiChatMsgAck;
using TaskDeleteSessionReq = common::message::TaskDeleteSessionReq;
using TaskSync = common::message::TaskSync;
using UserLoginSessionReq = common::message::UserLoginSessionReq;
using UserLoginSessionResp = common::message::UserLoginSessionResp;
using UserLogoutSessionReq = common::message::UserLogoutSessionReq;
using UserLogoutSessionResp = common::message::UserLogoutSessionResp;
using UserRegisterSessionReq = common::message::UserRegisterSessionReq;

SessionData::SessionData(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr)
    : fw::EoBase<SessionData>(cfg), accessGatewayAddr_(std::move(accessGatewayAddr))
{
    sendTo(accessGatewayAddr_, common::message::TempConfig{2});
}

void SessionData::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 3)
    {
        routerAddr_ = senderAddress();
    }
}

void SessionData::handle(AiChatBusinessReq req)
{
    auto gtid = req.head.gtidList.empty() ? common::kInvalidGtid : req.head.gtidList.at(0);
    LG_DBG("received AiChatBusinessReq: gtid=0x%x contentSize=%zuB", gtid, req.content.size());

    if (not routerAddr_)
    {
        LG_ERR("routerAddr not set");
        return;
    }

    delegateTo(routerAddr_, std::move(req));
}

void SessionData::handle(AiChatBusinessResp resp)
{
    auto gtid = resp.head.gtidList.empty() ? common::kInvalidGtid : resp.head.gtidList.at(0);
    LG_DBG("received AiChatBusinessResp: gtid=0x%x contentSize=%zuB", gtid, resp.content.size());

    if (not accessGatewayAddr_)
    {
        LG_ERR("accessGatewayAddr not set, dropping AiChatBusinessResp");
        return;
    }

    resp.head.targets = userAccessBitset_.at(resp.head.uid);
    delegateTo(accessGatewayAddr_, std::move(resp));
}

void SessionData::handle(AiChatMsgAck ack)
{
    auto gtid = ack.head.gtidList.empty() ? common::kInvalidGtid : ack.head.gtidList.at(0);
    LG_DBG("received AiChatMsgAck: gtid=0x%x seq=%u contentSize=%zuB", gtid, ack.seq,
           ack.content.size());

    if (not accessGatewayAddr_)
    {
        LG_ERR("accessGatewayAddr not set, dropping AiChatMsgAck");
        return;
    }

    ack.head.targets = userAccessBitset_.at(ack.head.uid);
    delegateTo(accessGatewayAddr_, std::move(ack));
}

void SessionData::handle(const UserLoginSessionReq &req)
{
    LG_DBG("UserLoginSessionReq: uid=0x%x gtids=%zu", req.head.uid, req.gtids.size());

    setAccessBit(req.head.uid, req.head.accessType);

    UserLoginSessionResp resp;
    resp.head = req.head;
    resp.needWaitForData = false;
    replyToSender(std::move(resp));
}

void SessionData::handle(const UserRegisterSessionReq &req)
{
    LG_DBG("UserRegisterSessionReq: userId=%d", static_cast<int>(req.userId));

    for (size_t i = 0; i < common::kMaxAppTypes; ++i)
    {
        auto uid = common::makeUid(req.userId, static_cast<common::AppType>(i));
        userAccessBitset_.at(uid) = 0;
    }
}

void SessionData::handle(const TaskDeleteSessionReq &req)
{
    if (req.head.gtidList.empty())
    {
        LG_ERR("TaskDeleteSessionReq has empty gtidList");
        return;
    }
    auto gtid = req.head.gtidList.at(0);
    LG_DBG("TaskDeleteSessionReq: uid=0x%x gtid=0x%x", req.head.uid, gtid);

    TaskSync sync;
    sync.head = req.head;
    sync.head.targets = userAccessBitset_.at(req.head.uid);
    sync.type = common::TaskSyncType::TaskDeleted;
    sync.gtid = gtid;
    delegateTo(accessGatewayAddr_, std::move(sync));
}

void SessionData::handle(const UserLogoutSessionReq &req)
{
    LG_DBG("UserLogoutSessionReq: uid=0x%x", req.head.uid);

    clearAccessBit(req.head.uid, req.head.accessType);

    UserLogoutSessionResp resp;
    resp.head = req.head;
    resp.activeAdapterCount = countActiveAdapters(req.head.uid);
    replyToSender(std::move(resp));
}

void SessionData::setAccessBit(uint16_t uid, common::AccessType accessType)
{
    auto idx = static_cast<size_t>(accessType);
    userAccessBitset_.at(uid) |= (1ULL << idx);
}

void SessionData::clearAccessBit(uint16_t uid, common::AccessType accessType)
{
    auto idx = static_cast<size_t>(accessType);
    userAccessBitset_.at(uid) &= ~(1ULL << idx);
}

uint8_t SessionData::countActiveAdapters(uint16_t uid) const
{
    return static_cast<uint8_t>(__builtin_popcountll(userAccessBitset_.at(uid)));
}

} // namespace DPlane::session
