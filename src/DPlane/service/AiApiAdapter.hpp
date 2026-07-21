// src/DPlane/service/AiApiAdapter.hpp
// Data-plane service layer — AI API communication adapter
//
// 独立线程运行（detached actor），在 handler 中通过 libcurl 执行同步 HTTP。
// 阻塞仅影响私有线程，不阻塞共享 Actor 调度池。

#pragma once

#include "fw/EoBase.hpp"
#include "utils/HttpClient.hpp"

#include <string>

namespace DPlane::service
{

class AiApiAdapter : public fw::EoBase<AiApiAdapter, true>
{
  public:
    AiApiAdapter(fw::EoConfig &cfg, std::string apiBaseUrl, std::string apiKey,
                 std::string defaultModel, fw::EoAddress routerAddr, fw::EoAddress serviceMgrAddr,
                 fw::EoAddress serviceGatewayAddr);

    void handle(const common::message::AiChatServiceReq &req);

  protected:
    void init() override
    {
        onMsg<common::message::AiChatServiceReq>();
    }

  private:
    utils::HttpClient::Response callApi(const common::message::AiChatServiceReq &req);

    std::string apiBaseUrl_;
    std::string apiKey_;
    std::string defaultModel_;
    fw::EoAddress routerAddr_;
    fw::EoAddress serviceMgrAddr_;
};

} // namespace DPlane::service
