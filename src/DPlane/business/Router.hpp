// src/DPlane/business/Router.hpp
// Data-plane business layer — Message Router EO
//
// 职责：根据 GTID[11:6] 提取 TaskType，将入向消息路由到对应 Business EO。
// 仅发往 Business D 面 EO 的消息经过 Router（ADR-0011）。

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
    void handle(const common::message::AiChatReq &req);
    void handle(const common::message::AiChatResp &resp);

  protected:
    void init() override
    {
        onMsg<common::message::ModifyReq>();
        onMsg<common::message::InternalPing>();
        onMsg<common::message::AiChatReq>();
        onMsg<common::message::AiChatResp>();
    }

  private:
    fw::EoAddress businessMgr;
    fw::EoAddress aiChatBusAddr;

  public:
    // 地址注入（由 main 在 spawn 后调用）
    void setAiChatBusAddr(fw::EoAddress addr)
    {
        aiChatBusAddr = addr;
    }
};

} // namespace DPlane::business
