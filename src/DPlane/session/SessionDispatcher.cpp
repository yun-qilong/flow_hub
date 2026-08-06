#include "DPlane/session/SessionDispatcher.hpp"

namespace DPlane::session
{

using AiChatReq = common::message::AiChatReq;
using AiChatResp = common::message::AiChatResp;

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

void SessionDispatcher::handle(AiChatReq req)
{
    LG_DBG("received AiChatReq: sessionTaskId=0x%x", req.head.sessionTaskId);

    if (not routerAddr_)
    {
        LG_ERR("routerAddr not set");
        return;
    }

    delegateTo(routerAddr_, std::move(req));
}

void SessionDispatcher::handle(AiChatResp resp)
{
    LG_DBG("received AiChatResp: sessionTaskId=0x%x", resp.head.sessionTaskId);

    if (not accessGatewayAddr_)
    {
        LG_ERR("accessGatewayAddr not set, dropping AiChatResp");
        return;
    }

    delegateTo(accessGatewayAddr_, std::move(resp));
}

} // namespace DPlane::session
