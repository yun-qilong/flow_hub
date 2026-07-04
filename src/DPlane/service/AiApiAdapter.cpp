// src/DPlane/service/AiApiAdapter.cpp

#include "DPlane/service/AiApiAdapter.hpp"
#include "utils/HttpClient.hpp"
#include "utils/JsonCoDec.hpp"

#include <iostream>

namespace DPlane::service
{

using namespace common::message;

AiApiAdapter::AiApiAdapter(fw::EoConfig &cfg, std::string apiBaseUrl, std::string apiKey,
                           std::string defaultModel, fw::EoAddress routerAddr,
                           fw::EoAddress serviceMgrAddr, fw::EoAddress serviceGatewayAddr)
    : fw::EoBase<AiApiAdapter>(cfg), apiBaseUrl_(std::move(apiBaseUrl)), apiKey_(std::move(apiKey)),
      defaultModel_(std::move(defaultModel)), routerAddr_(std::move(routerAddr)),
      serviceMgrAddr_(std::move(serviceMgrAddr))
{
    sendTo(serviceGatewayAddr, common::message::TempConfig{10});
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
    std::cout << "[AiApiAdapter] HTTP request"
              << " model=" << req.modelName << " temp=" << req.temperature
              << " msgSize=" << req.messagesJson.size() << "B\n";

    auto response = callApi(req);

    AiChatServiceResp resp;
    resp.head.gtidList = req.head.gtidList;
    resp.success = response.isSuccess();
    if (response.isSuccess())
    {
        resp.content = utils::JsonCoDec::extractContent(response.body);
    }
    else
    {
        resp.content = response.body;
    }

    std::cout << "[AiApiAdapter] HTTP " << response.httpCode << "\n";

    if (routerAddr_)
    {
        sendTo(routerAddr_, std::move(resp));
    }
    else
    {
        std::cerr << "[AiApiAdapter] WARNING: routerAddr not set, dropping response\n";
    }
}

} // namespace DPlane::service
