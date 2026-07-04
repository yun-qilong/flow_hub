// src/DPlane/service/ServiceMgr.hpp
// Control-plane service layer — Service Manager EO

#pragma once

#include "fw/EoBase.hpp"

namespace DPlane::service
{

class ServiceMgr : public fw::EoBase<ServiceMgr>
{
  public:
    explicit ServiceMgr(fw::EoConfig &cfg, fw::EoAddress businessMgrAddr);

    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
    }

  private:
    fw::EoAddress businessMgrAddr_;
    fw::EoAddress serviceGatewayAddr_;
    fw::EoAddress aiApiAdapterAddr_;
};

} // namespace DPlane::service
