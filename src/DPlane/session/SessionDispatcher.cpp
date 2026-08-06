#include "DPlane/session/SessionDispatcher.hpp"

#include <utility>

namespace DPlane::session
{

using AiChatReq = common::message::AiChatReq;

SessionDispatcher::SessionDispatcher(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr)
    : fw::EoBase<SessionDispatcher>(cfg)
{
    sendTo(std::move(accessGatewayAddr), common::message::TempConfig{2});
}

void SessionDispatcher::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 3)
    {
        routerAddr_ = senderAddress();
    }
    else if (msg.tag == 7)
    {
        orchestratorTable_.at(static_cast<size_t>(common::TaskType::AiAgora)) = senderAddress();
    }
    else
    {
        LG_WRN("unknown TempConfig tag=%u", static_cast<unsigned>(msg.tag));
    }
}

void SessionDispatcher::handle(AiChatReq req)
{
    LG_DBG("received AiChatReq: sessionTaskId=0x%x", static_cast<unsigned>(req.head.sessionTaskId));

    if (not routerAddr_)
    {
        LG_ERR("routerAddr not set");
        return;
    }

    delegateTo(routerAddr_, std::move(req));
}

} // namespace DPlane::session
