// src/CPlane/BusinessMgr.cpp

#include "CPlane/BusinessMgr.hpp"

#include <iostream>

namespace CPlane
{
BusinessMgr::BusinessMgr(EoConfig &cfg, EoAddress sessionMgrAddress)
    : EoBase<BusinessMgr>(cfg), sessionMgrAddr(std::move(sessionMgrAddress))
{
    sendTo(sessionMgrAddr, TempConfig{4});
}

void BusinessMgr::handle(const TempConfig &msg)
{
    if (msg.tag == 5)
    {
        routerAddr = senderAddress();
    }
    else if (msg.tag == 7)
    {
        serviceMgrAddr = senderAddress();
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
