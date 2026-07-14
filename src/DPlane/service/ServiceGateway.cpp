// src/DPlane/service/ServiceGateway.cpp

#include "DPlane/service/ServiceGateway.hpp"

#include <utility>

namespace DPlane::service
{

ServiceGateway::ServiceGateway(fw::EoConfig &cfg, fw::EoAddress serviceMgrAddr,
                               fw::EoAddress aiChatBusAddr)
    : fw::EoBase<ServiceGateway>(cfg)
{
    sendTo(std::move(serviceMgrAddr), common::message::TempConfig{8});
    sendTo(std::move(aiChatBusAddr), common::message::TempConfig{9});
}

void ServiceGateway::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 10)
    {
        aiApiAdapter_ = senderAddress();
    }
}

void ServiceGateway::handle(common::message::AiChatServiceReq req)
{
    LG_DBG(">>> AiChatServiceReq model=%s temp=%.1f msgSize=%zuB", req.modelName.c_str(),
           req.temperature, req.messagesJson.size());

    if (not aiApiAdapter_)
    {
        LG_ERR("adapterAddr not set");
        return;
    }

    delegateTo(aiApiAdapter_, std::move(req));
}

} // namespace DPlane::service
