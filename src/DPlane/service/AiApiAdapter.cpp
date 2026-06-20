// src/DPlane/service/AiApiAdapter.cpp

#include "DPlane/service/AiApiAdapter.hpp"
#include "utils/HttpClient.hpp"
#include "utils/JsonCoDec.hpp"

#include <iostream>

namespace DPlane::service
{

using namespace common::message;

AiApiAdapter::AiApiAdapter(fw::EoConfig &cfg, std::string apiBaseUrl, std::string apiKey,
                           std::string defaultModel)
    : fw::EoBase<AiApiAdapter>(cfg), apiBaseUrl_(std::move(apiBaseUrl)), apiKey_(std::move(apiKey)),
      defaultModel_(std::move(defaultModel))
{
}

void AiApiAdapter::init()
{
    onMsg<AiChatServiceReq>();
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
    if (response.isSuccess())
    {
        resp.content = utils::JsonCoDec::extractContent(response.body);
        resp.success = true;
    }
    else
    {
        resp.success = false;
        resp.errorMsg = response.body;
    }

    std::cout << "[AiApiAdapter] HTTP " << response.httpCode
              << " success=" << (resp.success ? "true" : "false") << "\n";

    auto targetAddr = req.head.sourceAddress;
    if (targetAddr)
    {
        sendTo(targetAddr, std::move(resp));
    }
    else
    {
        std::cerr << "[AiApiAdapter] WARNING: no sourceAddress in request, dropping response\n";
    }
}

} // namespace DPlane::service
