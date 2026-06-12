// src/DPlane/business/Router.cpp

#include "DPlane/business/Router.hpp"

#include <iostream>

namespace DPlane::business
{

using namespace fw;
using namespace common::message;

Router::Router(ActorConfig &cfg) : EoBase<Router>(cfg) {}

void Router::init()
{
    onMsg<ModifyReq>();
    onMsg<InternalPing>();
}

void Router::handle(const ModifyReq &req)
{
    std::cout << "[Router] received ModifyReq, updating BusinessMgr handle\n";
    businessMgr = req.handle;
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

} // namespace DPlane::business
