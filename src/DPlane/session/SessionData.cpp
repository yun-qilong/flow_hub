// src/DPlane/session/SessionData.cpp

#include "DPlane/session/SessionData.hpp"

namespace DPlane::session
{

using AiChatBusinessReq = common::message::AiChatBusinessReq;
using AiChatBusinessResp = common::message::AiChatBusinessResp;

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

    delegateTo(accessGatewayAddr_, std::move(resp));
}

} // namespace DPlane::session
