// src/CPlane/BusinessMgr.hpp
// Control-plane business layer — Business Manager EO

#pragma once

#include "fw/EoBase.hpp"

namespace CPlane
{
using EoConfig = fw::EoConfig;
using EoAddress = fw::EoAddress;
using TempConfig = common::message::TempConfig;
class BusinessMgr : public fw::EoBase<BusinessMgr>
{
  public:
    explicit BusinessMgr(EoConfig &cfg, EoAddress sessionMgrAddress);

    void handle(const TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<TempConfig>();
    }

  private:
    EoAddress sessionMgrAddr;
    EoAddress routerAddr;
    EoAddress serviceMgrAddr;
};

} // namespace CPlane
