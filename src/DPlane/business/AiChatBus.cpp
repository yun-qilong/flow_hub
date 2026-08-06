#include "DPlane/business/AiChatBus.hpp"
#include "common/Constants.hpp"
#include "common/TaskPool.hpp"
#include "utils/Result.hpp"
#include "utils/TryCatch.hpp"

#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace DPlane::business
{

using TaskPool = common::TaskPool;
using AiChatConfigReq = common::message::AiChatConfigReq;
using AiChatConfigResp = common::message::AiChatConfigResp;
using AiChatReq = common::message::AiChatReq;
using AiChatResp = common::message::AiChatResp;
using AiChatServiceReq = common::message::AiChatServiceReq;
using AiChatServiceResp = common::message::AiChatServiceResp;
using UserHead = common::message::UserHead;

namespace
{

constexpr uint8_t kMaxDebateAiIndex = 7;
constexpr uint8_t kJudgeIndex = 0xFE;
constexpr uint8_t kInvalidAiIndex = 0xFF;

struct ConfigFields
{
    std::string model;
    std::string apiUrl;
    std::string apiKey;
    double temperature = 0.0;
};

utils::Result<ConfigFields> extractConfigFields(const std::string &payload)
{
    return utils::tryOrFailed(
        [&]() -> utils::Result<ConfigFields>
        {
            auto root = nlohmann::json::parse(payload);
            if (not root.is_object() or not root.at("model").is_string() or
                not root.at("apiUrl").is_string() or not root.at("apiKey").is_string() or
                not root.at("temperature").is_number())
            {
                return std::nullopt;
            }
            ConfigFields fields;
            fields.model = root.at("model").get<std::string>();
            fields.apiUrl = root.at("apiUrl").get<std::string>();
            fields.apiKey = root.at("apiKey").get<std::string>();
            fields.temperature = root.at("temperature").get<double>();
            return utils::Result<ConfigFields>{std::move(fields)};
        },
        []() { return utils::Result<ConfigFields>{std::nullopt}; });
}

} // namespace

template <common::TaskType T>
AiChatBus<T>::AiChatBus(fw::EoConfig &cfg, TaskPool &pool, fw::EoAddress sessionDispatcherAddr,
                        fw::EoAddress businessMgrAddr, fw::EoAddress routerAddr)
    : fw::EoBase<AiChatBus<T>>(cfg), pool_(pool),
      sessionDispatcherAddr_(std::move(sessionDispatcherAddr)),
      businessMgrAddr_(std::move(businessMgrAddr)), routerAddr_(std::move(routerAddr))
{
    this->sendTo(routerAddr_, common::message::TempConfig{6});
}

template <common::TaskType T>
void AiChatBus<T>::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 9)
    {
        serviceGatewayAddr_ = this->senderAddress();
    }
}

template <common::TaskType T>
bool AiChatBus<T>::applyConfig(ContextType &ctx, const AiChatConfigReq &req)
{
    return extractConfigFields(req.payload)
        .useOrFailed(
            [&](ConfigFields &fields)
            {
                if (not writeCString(ctx.modelName, fields.model) or
                    not writeCString(ctx.apiUrl, fields.apiUrl) or
                    not writeCString(ctx.apiKey, fields.apiKey) or
                    not writeCString(ctx.systemPrompt, req.systemPrompt))
                {
                    return false;
                }
                ctx.temperature = fields.temperature;
                ctx.aiIndex = req.aiIndex;
                return true;
            },
            []() { return false; });
}

template <common::TaskType T>
void AiChatBus<T>::sendConfigResp(const UserHead &head, bool isSuccess)
{
    this->sendTo(sessionDispatcherAddr_, AiChatConfigResp{head, isSuccess});
}

template <common::TaskType T>
void AiChatBus<T>::handle(const AiChatConfigReq &req)
{
    pool_.getContext<ContextType>(firstBusTaskId(req.head))
        .useOrFailed(
            [&](ContextType &ctx)
            {
                bool isSuccess = isValidAiIndex(req.aiIndex) and applyConfig(ctx, req);
                sendConfigResp(req.head, isSuccess);
            },
            [&]()
            {
                LG_WRN("AiChatBus: context not found, busTaskIds.size=%zu, gtid=%u",
                       req.head.busTaskIds.size(), static_cast<unsigned>(firstBusTaskId(req.head)));
                sendConfigResp(req.head, false);
            });
}

template <common::TaskType T>
bool AiChatBus<T>::isValidAiIndex(uint8_t aiIndex) const
{
    return aiIndex <= kMaxDebateAiIndex or aiIndex == kJudgeIndex;
}

template <common::TaskType T>
void AiChatBus<T>::sendChatToService(const ContextType &ctx, const std::string &messagesJson,
                                     const UserHead &head)
{
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"}, {"content", readCString(ctx.systemPrompt)}});
    auto parsed = nlohmann::json::parse(messagesJson);
    if (not parsed.is_array())
    {
        LG_WRN("AiChatBus: messagesJson must be an array");
        throw std::invalid_argument("");
    }
    messages.insert(messages.end(), parsed.begin(), parsed.end());
    this->sendTo(serviceGatewayAddr_, buildServiceReq(head, messages.dump(), ctx));
}

template <common::TaskType T>
common::GTID AiChatBus<T>::firstBusTaskId(const UserHead &head) const
{
    if (head.busTaskIds.empty())
    {
        return common::kInvalidGtid;
    }
    return head.busTaskIds.front();
}

template <common::TaskType T>
AiChatServiceReq AiChatBus<T>::buildServiceReq(const UserHead &head,
                                               const std::string &messagesJson,
                                               const ContextType &ctx) const
{
    return AiChatServiceReq{head, messagesJson, readCString(ctx.modelName), ctx.temperature, 0};
}

template <common::TaskType T>
void AiChatBus<T>::sendChatResp(const UserHead &head, bool success, const std::string &content,
                                uint8_t aiIndex)
{
    this->sendTo(sessionDispatcherAddr_, AiChatResp{head, success, aiIndex, content});
}

template <common::TaskType T>
void AiChatBus<T>::handle(const AiChatReq &req)
{
    pool_.getContext<ContextType>(firstBusTaskId(req.head))
        .useOrFailed(
            [&](ContextType &ctx)
            {
                utils::tryOrFailed(
                    [&]() { sendChatToService(ctx, req.messagesJson, req.head); },
                    [&]()
                    {
                        LG_WRN("AiChatBus: invalid messagesJson, chat request rejected");
                        sendChatResp(req.head, false, "", ctx.aiIndex);
                    });
            },
            [&]()
            {
                LG_WRN("AiChatBus: context not found, busTaskIds.size=%zu, gtid=%u",
                       req.head.busTaskIds.size(), static_cast<unsigned>(firstBusTaskId(req.head)));
                sendChatResp(req.head, false, "", kInvalidAiIndex);
            });
}

template <common::TaskType T>
void AiChatBus<T>::handle(const AiChatServiceResp &resp)
{
    pool_.getContext<ContextType>(firstBusTaskId(resp.head))
        .useOrFailed([&](ContextType &ctx)
                     { sendChatResp(resp.head, resp.success, resp.content, ctx.aiIndex); },
                     [&]()
                     {
                         LG_WRN("AiChatBus: context not found, busTaskIds.size=%zu, gtid=%u",
                                resp.head.busTaskIds.size(),
                                static_cast<unsigned>(firstBusTaskId(resp.head)));
                     });
}

template <common::TaskType T>
template <size_t N>
std::string AiChatBus<T>::readCString(const std::array<uint8_t, N> &data) const
{
    std::string out;
    for (size_t i = 0; i < N and data.at(i) != 0; ++i)
    {
        out.push_back(static_cast<char>(data.at(i)));
    }
    return out;
}

template <common::TaskType T>
template <size_t N>
bool AiChatBus<T>::writeCString(std::array<uint8_t, N> &data, const std::string &value)
{
    if (value.size() >= N)
    {
        LG_WRN("AiChatBus: writeCString value too long, size=%zu, capacity=%zu", value.size(), N);
        return false;
    }
    std::memcpy(data.data(), value.data(), value.size());
    data.at(value.size()) = 0;
    return true;
}

template class AiChatBus<common::TaskType::AiChat>;

} // namespace DPlane::business
