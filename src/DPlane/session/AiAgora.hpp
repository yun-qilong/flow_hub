#pragma once

#include "fw/EoBase.hpp"

namespace DPlane::session
{

class AiAgora : public fw::EoBase<AiAgora>
{
  public:
    explicit AiAgora(fw::EoConfig &cfg, fw::EoAddress sessionDispatcherAddr);

    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
    }
};

} // namespace DPlane::session
