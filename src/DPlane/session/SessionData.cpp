// src/DPlane/session/SessionData.cpp

#include "DPlane/session/SessionData.hpp"

#include "common/UidUtil.hpp"

#include <iostream>

namespace DPlane::session
{

using namespace common::message;

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
    auto gtid = req.head.gtidList.empty() ? common::kInvalidGtid : req.head.gtidList[0];
    std::cout << "[SessionData] received AiChatBusinessReq: gtid=0x" << std::hex << gtid << std::dec
              << " content=" << req.content << "\n";

    if (not routerAddr_)
    {
        std::cerr << "[SessionData] ERROR: routerAddr not set\n";
        return;
    }

    delegateTo(routerAddr_, std::move(req));
}

void SessionData::handle(AiChatBusinessResp resp)
{
    auto gtid = resp.head.gtidList.empty() ? common::kInvalidGtid : resp.head.gtidList[0];
    std::cout << "[SessionData] received AiChatBusinessResp: gtid=0x" << std::hex << gtid
              << std::dec << " content=" << resp.content << "\n";

    if (not accessGatewayAddr_)
    {
        std::cerr
            << "[SessionData] ERROR: accessGatewayAddr not set, dropping AiChatBusinessResp\n";
        return;
    }

    resp.head.targets = userAccessBitset_.at(resp.head.uid);
    delegateTo(accessGatewayAddr_, std::move(resp));
}

void SessionData::handle(AiChatMsgAck ack)
{
    auto gtid = ack.head.gtidList.empty() ? common::kInvalidGtid : ack.head.gtidList[0];
    std::cout << "[SessionData] received AiChatMsgAck: gtid=0x" << std::hex << gtid << std::dec
              << " seq=" << ack.seq << " content=" << ack.content << "\n";

    if (not accessGatewayAddr_)
    {
        std::cerr << "[SessionData] ERROR: accessGatewayAddr not set, dropping AiChatMsgAck\n";
        return;
    }

    ack.head.targets = userAccessBitset_.at(ack.head.uid);
    delegateTo(accessGatewayAddr_, std::move(ack));
}

void SessionData::handle(const UserLoginSessionReq &req)
{
    std::cout << "[SessionData] UserLoginSessionReq: uid=0x" << std::hex << req.head.uid << std::dec
              << " gtids=" << req.gtids.size() << "\n";

    setAccessBit(req.head.uid, req.head.accessType);

    UserLoginSessionResp resp;
    resp.head = req.head;
    resp.needWaitForData = false;
    replyToSender(std::move(resp));
}

void SessionData::handle(const UserRegisterSessionReq &req)
{
    std::cout << "[SessionData] UserRegisterSessionReq: userId=" << static_cast<int>(req.userId)
              << "\n";

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
        std::cerr << "[SessionData] ERROR: TaskDeleteSessionReq has empty gtidList\n";
        return;
    }
    auto gtid = req.head.gtidList[0];
    std::cout << "[SessionData] TaskDeleteSessionReq: uid=0x" << std::hex << req.head.uid
              << std::dec << " gtid=0x" << std::hex << gtid << std::dec << "\n";

    TaskSync sync;
    sync.head = req.head;
    sync.head.targets = userAccessBitset_.at(req.head.uid);
    sync.type = common::TaskSyncType::TaskDeleted;
    sync.gtid = gtid;
    delegateTo(accessGatewayAddr_, std::move(sync));
}

void SessionData::handle(const UserLogoutSessionReq &req)
{
    std::cout << "[SessionData] UserLogoutSessionReq: uid=0x" << std::hex << req.head.uid
              << std::dec << "\n";

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
