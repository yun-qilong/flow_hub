// src/DPlane/service/ServiceGateway.cpp

#include "DPlane/service/ServiceGateway.hpp"

#include <iostream>

namespace DPlane::service
{

using namespace common::message;

ServiceGateway::ServiceGateway(fw::EoConfig &cfg, fw::EoAddress serviceMgrAddr, fw::EoAddress aiChatBusAddr)
    : fw::EoBase<ServiceGateway>(cfg)
{
    sendTo(serviceMgrAddr, common::message::TempConfig{8});
    sendTo(aiChatBusAddr, common::message::TempConfig{9});
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
    std::cout << "[ServiceGateway] >>> AiChatServiceReq"
              << " model=" << req.modelName << " temp=" << req.temperature
              << " msgSize=" << req.messagesJson.size() << "B\n";

    if (not aiApiAdapter_)
    {
        std::cerr << "[ServiceGateway] ERROR: adapterAddr not set\n";
        return;
    }

    delegateTo(aiApiAdapter_, std::move(req));
}

} // namespace DPlane::service
