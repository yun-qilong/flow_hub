// src/DPlane/business/Router.hpp
// Data-plane business layer — Message Router EO

#pragma once

#include "fw/ActorBase.hpp"

namespace DPlane::business
{

class Router : public fw::ActorBase<Router>
{
  public:
    explicit Router(fw::ActorConfig &cfg);

    // message handlers
    void handle(const common::message::ModifyReq &req);
    void handle(const common::message::InternalPing &msg);

  protected:
    void init() override;

  private:
    fw::ActorRef businessMgr;
};

} // namespace DPlane::business
