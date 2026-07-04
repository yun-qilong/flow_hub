// src/CPlane/BusinessMgr.cpp

#include "CPlane/BusinessMgr.hpp"

#include <iostream>

namespace CPlane
{

using namespace fw;
using namespace common::message;

BusinessMgr::BusinessMgr(EoConfig &cfg, fw::EoAddress sessionMgrAddr)
    : EoBase<BusinessMgr>(cfg), sessionMgrAddr_(std::move(sessionMgrAddr))
{
    sendTo(sessionMgrAddr_, common::message::TempConfig{4});
}

void BusinessMgr::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 5)
    {
        routerAddr_ = senderAddress();
    }
    else if (msg.tag == 7)
    {
        serviceMgrAddr_ = senderAddress();
    }
}

void BusinessMgr::handle(const InternalPong &msg)
{
    std::cout << "[BusinessMgr] received InternalPong: number=" << msg.number
              << ", message=" << msg.message << "\n";
    std::cout << "[BusinessMgr] done, quitting.\n";
    stop();
}

} // namespace CPlane
