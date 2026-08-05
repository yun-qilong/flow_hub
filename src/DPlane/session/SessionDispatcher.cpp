#include "DPlane/session/SessionDispatcher.hpp"

namespace DPlane::session
{

using AiChatBusinessReq = common::message::AiChatBusinessReq;
using AiChatBusinessResp = common::message::AiChatBusinessResp;

SessionDispatcher::SessionDispatcher(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr)
    : fw::EoBase<SessionDispatcher>(cfg), accessGatewayAddr_(std::move(accessGatewayAddr))
{
    sendTo(accessGatewayAddr_, common::message::TempConfig{2});
}

void SessionDispatcher::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 3)
    {
        routerAddr_ = senderAddress();
    }
}

void SessionDispatcher::handle(AiChatBusinessReq req)
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

void SessionDispatcher::handle(AiChatBusinessResp resp)
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
