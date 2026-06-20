// src/DPlane/business/Router.cpp

#include "DPlane/business/Router.hpp"

#include <iostream>

namespace DPlane::business
{

using namespace fw;
using namespace common::message;

Router::Router(EoConfig &cfg) : EoBase<Router>(cfg) {}

void Router::handle(const ModifyReq &req)
{
    std::cout << "[Router] received ModifyReq, setting aiChatBusAddr\n";
    aiChatBusAddr = req.handle;
}

void Router::handle(const InternalPing &msg)
{
    if (businessMgr)
    {
        std::cout << "[Router] received: " << msg.message << "\n";
        std::cout << "[Router] forwarding to BusinessMgr...\n";
        sendTo(businessMgr, msg);
        sendTo(businessMgr, InternalPong{1, msg.message});
        std::cout << "[Router] done, quitting.\n";
        stop();
    }
    else
    {
        std::cout << "[Router] BusinessMgr not bound, dropping ping\n";
    }
}

void Router::handle(const AiChatReq &req)
{
    auto gtid = req.head.gtidList.empty() ? static_cast<uint16_t>(0) : req.head.gtidList[0];
    std::cout << "[Router] received AiChatReq: gtid=0x" << std::hex << gtid << std::dec
              << " content=" << req.content << "\n";

    if (not aiChatBusAddr)
    {
        std::cerr << "[Router] ERROR: aiChatBusAddr not set, dropping AiChatReq\n";
        return;
    }

    // GTID>>6 = TaskType，Router 根据 TaskType 查表路由
    std::cout << "[Router] routing to AiChatBus\n";
    sendTo(aiChatBusAddr, AiChatReq{req.head, req.content});
}

void Router::handle(const AiChatResp &resp)
{
    auto gtid = resp.head.gtidList.empty() ? static_cast<uint16_t>(0) : resp.head.gtidList[0];
    std::cout << "[Router] received AiChatResp: gtid=0x" << std::hex << gtid << std::dec << "\n";

    if (not aiChatBusAddr)
    {
        std::cerr << "[Router] ERROR: aiChatBusAddr not set, dropping AiChatResp\n";
        return;
    }

    std::cout << "[Router] routing to AiChatBus\n";
    sendTo(aiChatBusAddr, AiChatResp{resp.head, resp.success, resp.content});
}

} // namespace DPlane::business
