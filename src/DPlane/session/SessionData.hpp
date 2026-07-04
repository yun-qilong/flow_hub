// src/DPlane/session/SessionData.hpp
// Data-plane session layer — Session Data EO
//
// 会话数据面，负责消息包装与转发

#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"

#include <array>
#include <cstdint>

namespace DPlane::session
{

class SessionData : public fw::EoBase<SessionData>
{
  public:
    explicit SessionData(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr);

    void handle(const common::message::AiChatBusinessReq &req);
    void handle(const common::message::AiChatBusinessResp &resp);
    void handle(const common::message::UserLoginSessionReq &req);
    void handle(const common::message::UserLogoutSessionReq &req);
    void handle(const common::message::UserRegisterSessionReq &req);
    void handle(const common::message::TaskDeleteSessionReq &req);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::AiChatBusinessReq>();
        onMsg<common::message::AiChatBusinessResp>();
        onMsg<common::message::UserLoginSessionReq>();
        onMsg<common::message::UserLogoutSessionReq>();
        onMsg<common::message::UserRegisterSessionReq>();
        onMsg<common::message::TaskDeleteSessionReq>();
    }

  private:
    void setAccessBit(uint16_t uid, common::AccessType accessType);
    void clearAccessBit(uint16_t uid, common::AccessType accessType);
    uint8_t countActiveAdapters(uint16_t uid) const;

    fw::EoAddress routerAddr_;
    fw::EoAddress accessGatewayAddr_;
    std::array<uint64_t, common::kMaxUid> userAccessBitset_{};
};

} // namespace DPlane::session
