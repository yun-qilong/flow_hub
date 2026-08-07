#include "DPlane/session/SessionDispatcher.hpp"

#include <utility>

namespace DPlane::session
{

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

} // namespace DPlane::session
