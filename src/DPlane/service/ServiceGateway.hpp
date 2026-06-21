// src/DPlane/service/ServiceGateway.hpp
// Data-plane service layer — Unified service entry (ADR-0012)
//
// 所有外部服务适配器经此统一入口进出。
// v1: 简单转发 + 日志，后续支持 fan-out（ADR-0013）。

#pragma once

#include "fw/EoBase.hpp"

namespace DPlane::service
{

class ServiceGateway : public fw::EoBase<ServiceGateway>
{
  public:
    explicit ServiceGateway(fw::EoConfig &cfg, fw::EoAddress adapterAddr);

    void handle(common::message::AiChatServiceReq req);

  protected:
    void init() override
    {
        onMsg<common::message::AiChatServiceReq>();
    }

  private:
    fw::EoAddress aiApiAdapter_; // AiApiAdapter
};

} // namespace DPlane::service
