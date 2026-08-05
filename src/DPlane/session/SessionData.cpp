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
    LG_DBG("received AiChatBusinessReq: sessionTaskId=0x%x contentSize=%zuB",
           req.head.sessionTaskId, req.content.size());

    if (not routerAddr_)
    {
        LG_ERR("routerAddr not set");
        return;
    }

    delegateTo(routerAddr_, std::move(req));
}

void SessionData::handle(AiChatBusinessResp resp)
{
    LG_DBG("received AiChatBusinessResp: sessionTaskId=0x%x contentSize=%zuB",
           resp.head.sessionTaskId, resp.content.size());

    if (not accessGatewayAddr_)
    {
        LG_ERR("accessGatewayAddr not set, dropping AiChatBusinessResp");
        return;
    }

    delegateTo(accessGatewayAddr_, std::move(resp));
}

} // namespace DPlane::session
