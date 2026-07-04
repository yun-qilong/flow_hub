// src/CPlane/BusinessMgr.hpp
// Control-plane business layer — Business Manager EO

#pragma once

#include "fw/EoBase.hpp"
#include "utils/Result.hpp"

namespace CPlane
{

class BusinessMgr : public fw::EoBase<BusinessMgr>
{
  public:
    explicit BusinessMgr(fw::EoConfig &cfg, fw::EoAddress sessionMgrAddr);

    void handle(const common::message::InternalPong &msg);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::InternalPong>();
    }

  private:
    fw::EoAddress sessionMgrAddr_;
    fw::EoAddress routerAddr_;
    fw::EoAddress serviceMgrAddr_;
};

} // namespace CPlane
