// src/DPlane/business/Router.hpp
// Data-plane business layer — Message Router EO

#pragma once

#include "fw/EoBase.hpp"

namespace DPlane::business
{

class Router : public fw::EoBase<Router>
{
  public:
    explicit Router(fw::EoConfig &cfg);

    // message handlers
    void handle(const common::message::ModifyReq &req);
    void handle(const common::message::InternalPing &msg);

  protected:
    void init() override;

  private:
    fw::EoAddress businessMgr;
};

} // namespace DPlane::business
