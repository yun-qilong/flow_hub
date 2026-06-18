// src/DPlane/service/AiApiAdapter.hpp
// Data-plane service layer — AI API communication adapter
//
// 独立线程运行（detached actor），在 handler 中通过 libcurl 执行同步 HTTP。
// 阻塞仅影响私有线程，不阻塞共享 Actor 调度池。

#pragma once

#include "fw/EoBase.hpp"
#include "generated/message/AiChatServiceReq.hpp"
#include "generated/message/AiChatServiceResp.hpp"
#include "utils/HttpClient.hpp"

#include <string>

namespace DPlane::service
{

class AiApiAdapter : public fw::EoBase<AiApiAdapter>
{
  public:
    static constexpr bool kMayBlock = true; // handler 中有阻塞 HTTP 调用
  public:
    AiApiAdapter(fw::EoConfig &cfg, std::string apiBaseUrl, std::string apiKey,
                 std::string defaultModel);

    void handle(const common::message::AiChatServiceReq &req);

  protected:
    void init() override;

  private:
    utils::HttpClient::Response callApi(const common::message::AiChatServiceReq &req);

    std::string apiBaseUrl_;
    std::string apiKey_;
    std::string defaultModel_;
};

} // namespace DPlane::service
