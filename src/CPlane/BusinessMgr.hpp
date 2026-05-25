// src/CPlane/BusinessMgr.hpp
// Control-plane business layer — Business Manager EO

#pragma once

#include "fw/ActorBase.hpp"
#include "utils/Result.hpp"

namespace CPlane
{

class BusinessMgr : public fw::ActorBase<BusinessMgr>
{
  public:
    explicit BusinessMgr(fw::ActorConfig &cfg);

    // message handlers
    void handle(const common::message::InternalPing &msg);
    void handle(const common::message::InternalPong &msg);

  protected:
    void init() override;

  private:
    // context — temporary placeholder for future static memory
    utils::Result<fw::ActorRef> context;
};

} // namespace CPlane
