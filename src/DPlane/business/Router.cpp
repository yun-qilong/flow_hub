// src/DPlane/business/Router.cpp

#include "DPlane/business/Router.hpp"

#include <iostream>

namespace DPlane::business
{

using namespace fw;
using namespace common::message;

Router::Router(EoConfig &cfg, fw::EoAddress businessMgrAddr, fw::EoAddress sessionDataAddr)
    : EoBase<Router>(cfg), businessMgrAddr_(std::move(businessMgrAddr))
{
    sendTo(businessMgrAddr_, common::message::TempConfig{5});
    sendTo(sessionDataAddr, common::message::TempConfig{3});
}

void Router::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 6)
    {
        auto idx = static_cast<uint16_t>(common::TaskType::AiChat);
        routeTable_.at(idx) = senderAddress();
    }
}

void Router::handle(const RouterConfigReq &req)
{
    std::cout << "[Router] received RouterConfigReq, installing route table\n";
    routeTable_ = req.addresses;
    replyToSender(RouterConfigResp{true});
}

void Router::handle(const RouterReconfigReq &req)
{
    std::cout << "[Router] received RouterReconfigReq, updating " << req.entries.size()
              << " entries\n";
    for (const auto &entry : req.entries)
    {
        auto idx = static_cast<uint16_t>(entry.taskType);
        routeTable_.at(idx) = entry.address;
    }
    replyToSender(RouterReconfigResp{true});
}

void Router::handle(AiChatBusinessReq req)
{
    std::cout << "[Router] received AiChatBusinessReq, content=" << req.content << "\n";
    routeAndForward(std::move(req), "AiChatBusinessReq");
}

void Router::handle(AiChatServiceResp resp)
{
    std::cout << "[Router] received AiChatServiceResp\n";
    routeAndForward(std::move(resp), "AiChatServiceResp");
}

} // namespace DPlane::business
