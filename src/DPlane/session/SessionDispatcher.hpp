#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"

namespace DPlane::session
{

class SessionDispatcher : public fw::EoBase<SessionDispatcher>
{
  public:
    explicit SessionDispatcher(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr);

    void handle(common::message::AiChatBusinessReq req);
    void handle(common::message::AiChatBusinessResp resp);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::AiChatBusinessReq>();
        onMsg<common::message::AiChatBusinessResp>();
    }

  private:
    fw::EoAddress routerAddr_;
    fw::EoAddress accessGatewayAddr_;
};

} // namespace DPlane::session
