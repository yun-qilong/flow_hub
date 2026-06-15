// src/CPlane/BusinessMgr.cpp

#include "CPlane/BusinessMgr.hpp"

#include <iostream>

namespace CPlane
{

using namespace fw;
using namespace common::message;

BusinessMgr::BusinessMgr(EoConfig &cfg) : EoBase<BusinessMgr>(cfg) {}

void BusinessMgr::init()
{
    onMsg<InternalPing>();
    onMsg<InternalPong>();
}

void BusinessMgr::handle(const InternalPing &msg)
{
    std::cout << "[BusinessMgr] received InternalPing: " << msg.message << "\n";
}

void BusinessMgr::handle(const InternalPong &msg)
{
    std::cout << "[BusinessMgr] received InternalPong: number=" << msg.number
              << ", message=" << msg.message << "\n";
    std::cout << "[BusinessMgr] done, quitting.\n";
    stop();
}

} // namespace CPlane
