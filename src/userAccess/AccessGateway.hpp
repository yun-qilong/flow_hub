#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"
#include "generated/message/Messages.hpp"

#include <array>
#include <cstdint>

namespace userAccess
{

class AccessGateway : public fw::EoBase<AccessGateway>
{
  public:
    AccessGateway(fw::EoConfig &cfg, const fw::EoAddress &cliAdapter);

    void handle(const common::message::UserRegisterReq &req);
    void handle(common::message::UserRegisterResp resp);
    void handle(const common::message::UserLoginReq &req);
    void handle(common::message::UserLoginResp resp);
    void handle(const common::message::UserLogoutReq &req);
    void handle(common::message::UserLogoutResp resp);
    void handle(const common::message::UserDeleteReq &req);
    void handle(common::message::UserDeleteResp resp);
    void handle(const common::message::TaskCreateReq &req);
    void handle(common::message::TaskCreateResp resp);
    void handle(const common::message::TaskDeleteReq &req);
    void handle(common::message::TaskDeleteResp resp);

    void handle(common::message::AiChatBusinessReq req);
    void handle(common::message::AiChatBusinessResp resp);
    void handle(common::message::AiChatMsgAck ack);
    void handle(common::message::TaskSync sync);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::UserRegisterReq>();
        onMsg<common::message::UserRegisterResp>();
        onMsg<common::message::UserLoginReq>();
        onMsg<common::message::UserLoginResp>();
        onMsg<common::message::UserLogoutReq>();
        onMsg<common::message::UserLogoutResp>();
        onMsg<common::message::UserDeleteReq>();
        onMsg<common::message::UserDeleteResp>();
        onMsg<common::message::TaskCreateReq>();
        onMsg<common::message::TaskCreateResp>();
        onMsg<common::message::TaskDeleteReq>();
        onMsg<common::message::TaskDeleteResp>();
        onMsg<common::message::AiChatBusinessReq>();
        onMsg<common::message::AiChatBusinessResp>();
        onMsg<common::message::AiChatMsgAck>();
        onMsg<common::message::TaskSync>();
        onMsg<common::message::TempConfig>();
    }

  private:
    template <typename Msg>
    void routeToAdapters(Msg &msg);

    template <typename Msg>
    void fanOutToAdapters(const Msg &msg, uint64_t targets);

    template <typename Msg>
    void forwardToAdapter(Msg &msg);

    fw::EoAddress sessionMgrAddr_;
    fw::EoAddress sessionDataAddr_;
    std::array<fw::EoAddress, common::kMaxAccessTypes> adapterTable_;
};

} // namespace userAccess
