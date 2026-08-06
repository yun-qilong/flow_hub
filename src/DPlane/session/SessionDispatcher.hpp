#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"

namespace DPlane::session
{

class SessionDispatcher : public fw::EoBase<SessionDispatcher>
{
  public:
    explicit SessionDispatcher(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr);

    void handle(common::message::AiChatReq req);
    void handle(common::message::AiChatResp resp);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::AiChatReq>();
        onMsg<common::message::AiChatResp>();
    }

  private:
    fw::EoAddress routerAddr_;
    fw::EoAddress accessGatewayAddr_;
};

} // namespace DPlane::session
