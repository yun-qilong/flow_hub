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
    explicit BusinessMgr(fw::EoConfig &cfg);

    // message handlers
    void handle(const common::message::InternalPing &msg);
    void handle(const common::message::InternalPong &msg);

  protected:
    void init() override
    {
        onMsg<common::message::InternalPing>();
        onMsg<common::message::InternalPong>();
    }
};

} // namespace CPlane
