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

    void handle(const common::message::TaskCreateReq &req);
    void handle(common::message::TaskCreateResp resp);
    void handle(const common::message::TaskDeleteReq &req);
    void handle(common::message::TaskDeleteResp resp);

    void handle(common::message::AiAgoraChatReq req);
    void handle(common::message::AiAgoraChatResp resp);
    void handle(common::message::AiAgoraResetReq req);
    void handle(common::message::AiAgoraResetResp resp);
    void handle(common::message::TaskConfigReq req);
    void handle(common::message::TaskConfigResp resp);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TaskCreateReq>();
        onMsg<common::message::TaskCreateResp>();
        onMsg<common::message::TaskDeleteReq>();
        onMsg<common::message::TaskDeleteResp>();
        onMsg<common::message::TaskConfigReq>();
        onMsg<common::message::TaskConfigResp>();
        onMsg<common::message::AiAgoraChatReq>();
        onMsg<common::message::AiAgoraChatResp>();
        onMsg<common::message::AiAgoraResetReq>();
        onMsg<common::message::AiAgoraResetResp>();
        onMsg<common::message::TempConfig>();
    }

  private:
    static constexpr size_t kGtidToAdapterSize = 4096;

    fw::EoAddress sessionMgrAddr_;
    fw::EoAddress sessionDispatcherAddr_;
    fw::EoAddress cliAdapterAddr_;
    std::array<fw::EoAddress, kGtidToAdapterSize> gtidToAdapter_{};
};

} // namespace userAccess
