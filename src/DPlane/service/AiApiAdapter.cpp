// src/DPlane/service/AiApiAdapter.cpp

#include "DPlane/service/AiApiAdapter.hpp"
#include "utils/HttpClient.hpp"
#include "utils/JsonCoDec.hpp"

#include <utility>

namespace DPlane::service
{
using AiChatServiceReq = common::message::AiChatServiceReq;
using AiChatServiceResp = common::message::AiChatServiceResp;

AiApiAdapter::AiApiAdapter(fw::EoConfig &cfg, std::string apiBaseUrl, std::string apiKey,
                           std::string defaultModel, fw::EoAddress routerAddr,
                           fw::EoAddress serviceMgrAddr, fw::EoAddress serviceGatewayAddr)
    : fw::EoBase<AiApiAdapter, true>(cfg), apiBaseUrl_(std::move(apiBaseUrl)),
      apiKey_(std::move(apiKey)), defaultModel_(std::move(defaultModel)),
      routerAddr_(std::move(routerAddr)), serviceMgrAddr_(std::move(serviceMgrAddr))
{
    sendTo(std::move(serviceGatewayAddr), common::message::TempConfig{10});
    sendTo(serviceMgrAddr_, common::message::TempConfig{11});
}

utils::HttpClient::Response AiApiAdapter::callApi(const AiChatServiceReq &req)
{
    std::string model = req.modelName.empty() ? defaultModel_ : req.modelName;
    std::string httpBody =
        utils::JsonCoDec::buildHttpBody(model, req.messagesJson, req.temperature);
    std::string url = apiBaseUrl_ + "/v1/chat/completions";

    return utils::HttpClient::postJson(url, httpBody, apiKey_);
}

void AiApiAdapter::handle(const AiChatServiceReq &req)
{
    LG_DBG("HTTP request model=%s temp=%.1f msgSize=%zuB", req.modelName.c_str(), req.temperature,
           req.messagesJson.size());

    auto response = callApi(req);

    AiChatServiceResp resp;
    resp.head = req.head;
    resp.reqSeq = req.reqSeq;
    resp.success = response.isSuccess();
    if (response.isSuccess())
    {
        resp.content = utils::JsonCoDec::extractContent(response.body);
    }
    else
    {
        resp.content = response.body;
    }

    LG_DBG("HTTP %d", response.httpCode);

    if (routerAddr_)
    {
        sendTo(routerAddr_, std::move(resp));
    }
    else
    {
        LG_WRN("routerAddr not set, dropping response");
    }
}

} // namespace DPlane::service
