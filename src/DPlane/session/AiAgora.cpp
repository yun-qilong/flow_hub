#include "DPlane/session/AiAgora.hpp"
#include "common/TaskPool.hpp"
#include "utils/TryCatch.hpp"

#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace DPlane::session
{

using AiAgoraContext = common::context::AiAgoraContext;
using TaskConfigReq = common::message::TaskConfigReq;
using TaskConfigResp = common::message::TaskConfigResp;
using BusTaskCreateReq = common::message::BusTaskCreateReq;
using BusTaskCreateResp = common::message::BusTaskCreateResp;
using BusTaskDeleteReq = common::message::BusTaskDeleteReq;
using AiChatConfigReq = common::message::AiChatConfigReq;
using AiChatConfigResp = common::message::AiChatConfigResp;
using TempConfig = common::message::TempConfig;
using UserHead = common::message::UserHead;

namespace
{

constexpr common::TaskType kAiChatTaskType = common::TaskType::AiChat;

uint32_t computeMaxCharPerTopic(uint16_t maxResponseLength, uint8_t aiCount, uint8_t maxRounds)
{
    auto raw = static_cast<double>(maxResponseLength) * aiCount * maxRounds *
               common::kContextRedundancyFactor;
    return static_cast<uint32_t>(std::ceil(raw));
}

std::string buildSystemPrompt(const std::string &basePrompt, uint16_t maxResponseLength)
{
    return basePrompt +
           "\n\nYou are in a one-on-one conversation with the user. "
           "Answer the question directly. Each response must not exceed " +
           std::to_string(maxResponseLength) +
           " characters. The orchestrator passes through your input and output verbatim "
           "without any prefix.";
}

std::string buildAiChatPayload(const std::string &apiUrl, const std::string &apiKey,
                               const std::string &model, double temperature)
{
    nlohmann::json root;
    root["apiUrl"] = apiUrl;
    root["apiKey"] = apiKey;
    root["model"] = model;
    root["temperature"] = temperature;
    return root.dump();
}

bool validateConfigEntries(const nlohmann::json &root, ParsedTaskConfig &out)
{
    for (const auto &elem : root.at("configs"))
    {
        if (not elem.is_object() or not elem.contains("apiUrl") or not elem.contains("apiKey") or
            not elem.contains("model") or not elem.contains("systemPrompt") or
            not elem.contains("temperature"))
        {
            return false;
        }
        AiConfigSpec spec;
        spec.apiUrl = elem.at("apiUrl").get<std::string>();
        spec.apiKey = elem.at("apiKey").get<std::string>();
        spec.model = elem.at("model").get<std::string>();
        spec.systemPrompt = elem.at("systemPrompt").get<std::string>();
        spec.temperature = elem.at("temperature").get<double>();
        out.configs.push_back(std::move(spec));
    }
    return true;
}

bool hasAllFields(const nlohmann::json &root)
{
    return root.is_object() and root.contains("aiCount") and root.contains("hasJudge") and
           root.contains("maxRounds") and root.contains("maxResponseLength") and
           root.contains("timeoutMs") and root.contains("configs");
}

bool validateConfigScalars(const nlohmann::json &root, ParsedTaskConfig &out)
{
    auto aiCount = root.at("aiCount").get<uint8_t>();
    auto hasJudge = root.at("hasJudge").get<bool>();
    auto maxRounds = root.at("maxRounds").get<uint8_t>();
    auto maxResponseLength = root.at("maxResponseLength").get<uint16_t>();
    auto timeoutMs = root.at("timeoutMs").get<uint32_t>();

    if (aiCount < 1 or aiCount > 8 or hasJudge != (aiCount > 1) or maxRounds < 1 or
        maxResponseLength < 1 or timeoutMs == 0)
    {
        return false;
    }

    out.aiCount = aiCount;
    out.hasJudge = hasJudge;
    out.maxRounds = maxRounds;
    out.maxResponseLength = maxResponseLength;
    out.timeoutMs = timeoutMs;
    out.maxCharPerTopic = computeMaxCharPerTopic(maxResponseLength, aiCount, maxRounds);
    return true;
}

bool validateConfigFields(const nlohmann::json &root, ParsedTaskConfig &out)
{
    if (not hasAllFields(root))
    {
        return false;
    }

    auto &configs = root.at("configs");
    auto aiCount = root.at("aiCount").get<uint8_t>();
    auto hasJudge = root.at("hasJudge").get<bool>();
    if (not configs.is_array() or
        configs.size() != static_cast<size_t>(aiCount) + (hasJudge ? 1U : 0U))
    {
        return false;
    }

    return validateConfigScalars(root, out);
}

bool validateAndBuildConfig(const nlohmann::json &root, ParsedTaskConfig &out)
{
    return validateConfigFields(root, out) and validateConfigEntries(root, out);
}

} // namespace

AiAgora::AiAgora(fw::EoConfig &cfg, fw::EoAddress sessionDispatcherAddr,
                 fw::EoAddress sessionMgrAddr, fw::EoAddress routerAddr,
                 fw::EoAddress accessGatewayAddr, common::TaskPool &pool)
    : fw::EoBase<AiAgora>(cfg), sessionMgrAddr_(std::move(sessionMgrAddr)),
      routerAddr_(std::move(routerAddr)), accessGatewayAddr_(std::move(accessGatewayAddr)),
      pool_(pool)
{
    sendTo(std::move(sessionDispatcherAddr), TempConfig{7});
}

void AiAgora::handle(const TempConfig & /*msg*/) {}

utils::Result<ParsedTaskConfig> AiAgora::validateAndExtractConfig(const std::string &payload)
{
    return utils::tryOrFailed(
        [&]() -> utils::Result<ParsedTaskConfig>
        {
            auto root = nlohmann::json::parse(payload);
            ParsedTaskConfig out;
            if (not validateAndBuildConfig(root, out))
            {
                return utils::Result<ParsedTaskConfig>{std::nullopt};
            }
            return utils::Result<ParsedTaskConfig>{std::move(out)};
        },
        []() { return utils::Result<ParsedTaskConfig>{std::nullopt}; });
}

std::vector<common::TaskType> AiAgora::buildTaskTypes(const ParsedTaskConfig &cfg)
{
    auto count = static_cast<size_t>(cfg.aiCount) + (cfg.hasJudge ? 1U : 0U);
    std::vector<common::TaskType> types;
    types.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        types.push_back(kAiChatTaskType);
    }
    return types;
}

uint16_t AiAgora::buildPendingBits(const ParsedTaskConfig &cfg)
{
    auto bits = static_cast<uint16_t>((1U << cfg.aiCount) - 1);
    if (cfg.hasJudge)
    {
        bits |= kJudgePendingBit;
    }
    return bits;
}

void AiAgora::handle(const TaskConfigReq &req)
{
    pool_.getContext<AiAgoraContext>(req.head.sessionTaskId)
        .useOrFailed(
            [&](AiAgoraContext &ctx)
            {
                if (ctx.state != kStateUnconfigured)
                {
                    LG_WRN("AiAgora: session 0x%x is not unconfigured", req.head.sessionTaskId);
                    sendTo(accessGatewayAddr_, TaskConfigResp{req.head, false, 0});
                    return;
                }
                processConfig(ctx, req);
            },
            [&]()
            {
                LG_WRN("AiAgora: no session context for 0x%x", req.head.sessionTaskId);
                sendTo(accessGatewayAddr_, TaskConfigResp{req.head, false, 0});
            });
}

void AiAgora::processConfig(AiAgoraContext &ctx, const TaskConfigReq &req)
{
    validateAndExtractConfig(req.payload)
        .useOrFailed(
            [&](ParsedTaskConfig &parsed)
            {
                ctx.state = kStateConfiguring;
                ctx.maxRounds = parsed.maxRounds;
                ctx.maxResponseLength = parsed.maxResponseLength;
                ctx.maxCharPerTopic = parsed.maxCharPerTopic;
                ctx.timeoutMs = parsed.timeoutMs;
                ctx.pendingReplies = 0;

                requestThen(
                    sessionMgrAddr_, std::chrono::milliseconds(parsed.timeoutMs),
                    BusTaskCreateReq{req.head, buildTaskTypes(parsed)},
                    [this, cfg = std::move(parsed)](BusTaskCreateResp &resp) mutable
                    { onBusTaskCreated(std::move(cfg), resp); },
                    [this, head = req.head](const caf::error &) { failConfig(head); });
            },
            [&]()
            {
                LG_WRN("AiAgora: invalid config payload for session 0x%x", req.head.sessionTaskId);
                sendTo(accessGatewayAddr_, TaskConfigResp{req.head, false, 0});
            });
}

void AiAgora::onBusTaskCreated(ParsedTaskConfig cfg, const BusTaskCreateResp &resp)
{
    pool_.getContext<AiAgoraContext>(resp.head.sessionTaskId)
        .useOrFailed(
            [&](AiAgoraContext &ctx)
            {
                if (ctx.state != kStateConfiguring)
                {
                    LG_WRN("AiAgora: stale BusTaskCreateResp for 0x%x", resp.head.sessionTaskId);
                    return;
                }
                if (not resp.isSuccess or resp.head.busTaskIds.size() < cfg.aiCount)
                {
                    LG_WRN("AiAgora: busTask create failed for session 0x%x",
                           resp.head.sessionTaskId);
                    ctx.state = kStateUnconfigured;
                    sendTo(accessGatewayAddr_, TaskConfigResp{resp.head, false, 0});
                    return;
                }
                applyBusTaskIds(ctx, cfg, resp.head.busTaskIds);
                ctx.pendingReplies = buildPendingBits(cfg);
                sendAiChatConfigs(resp.head, ctx, cfg);
            },
            [&]() { LG_WRN("AiAgora: no session context for 0x%x", resp.head.sessionTaskId); });
}

void AiAgora::applyBusTaskIds(AiAgoraContext &ctx, const ParsedTaskConfig &cfg,
                              const std::vector<common::GTID> &busTaskIds)
{
    for (uint8_t i = 0; i < cfg.aiCount; ++i)
    {
        ctx.debateTaskIds.at(i) = busTaskIds.at(i);
    }
    if (cfg.hasJudge)
    {
        ctx.judgeTaskId = busTaskIds.at(cfg.aiCount);
    }
    else
    {
        ctx.judgeTaskId = common::kInvalidGtid;
    }
}

void AiAgora::recycleBusTasks(const UserHead &head, AiAgoraContext &ctx)
{
    std::vector<common::GTID> gtids;
    gtids.reserve(9);
    for (uint8_t i = 0; i < 8; ++i)
    {
        gtids.push_back(ctx.debateTaskIds.at(i));
        ctx.debateTaskIds.at(i) = common::kInvalidGtid;
    }
    gtids.push_back(ctx.judgeTaskId);
    ctx.judgeTaskId = common::kInvalidGtid;
    auto reqHead = head;
    reqHead.busTaskIds = std::move(gtids);
    sendTo(sessionMgrAddr_, BusTaskDeleteReq{reqHead});
}

void AiAgora::sendAiChatConfigs(const UserHead &head, AiAgoraContext &ctx,
                                const ParsedTaskConfig &cfg)
{
    for (uint8_t i = 0; i < cfg.aiCount; ++i)
    {
        auto reqHead = head;
        reqHead.busTaskIds = {ctx.debateTaskIds.at(i)};
        auto systemPrompt =
            buildSystemPrompt(cfg.configs.at(i).systemPrompt, cfg.maxResponseLength);
        auto payload = buildAiChatPayload(cfg.configs.at(i).apiUrl, cfg.configs.at(i).apiKey,
                                          cfg.configs.at(i).model, cfg.configs.at(i).temperature);
        sendTo(routerAddr_, AiChatConfigReq{reqHead, i, systemPrompt, payload});
    }
}

void AiAgora::handle(const AiChatConfigResp &resp)
{
    pool_.getContext<AiAgoraContext>(resp.head.sessionTaskId)
        .useOrFailed([&](AiAgoraContext &ctx) { onAiChatConfigResp(ctx, resp); }, [&]()
                     { LG_WRN("AiAgora: no session context for 0x%x", resp.head.sessionTaskId); });
}

void AiAgora::onAiChatConfigResp(AiAgoraContext &ctx, const AiChatConfigResp &resp)
{
    if (ctx.state != kStateConfiguring)
    {
        LG_WRN("AiAgora: stale AiChatConfigResp for 0x%x", resp.head.sessionTaskId);
        return;
    }
    if (not resp.isSuccess)
    {
        LG_WRN("AiAgora: aiChat config failed for session 0x%x", resp.head.sessionTaskId);
        recycleBusTasks(resp.head, ctx);
        ctx.state = kStateUnconfigured;
        sendTo(accessGatewayAddr_, TaskConfigResp{resp.head, false, 0});
        return;
    }
    clearPendingBit(ctx, resp.aiIndex);
    if (ctx.pendingReplies == 0)
    {
        completeConfig(resp.head, ctx);
    }
}

void AiAgora::clearPendingBit(AiAgoraContext &ctx, uint8_t aiIndex)
{
    if (aiIndex == common::kJudgeIndex)
    {
        ctx.pendingReplies &= static_cast<uint16_t>(~kJudgePendingBit);
    }
    else
    {
        ctx.pendingReplies &= static_cast<uint16_t>(~(1U << aiIndex));
    }
}

void AiAgora::completeConfig(const UserHead &head, AiAgoraContext &ctx)
{
    ctx.state = kStateWaitingForTopic;
    auto estimated = static_cast<uint16_t>(common::kTopicBaseJsonSize / ctx.maxCharPerTopic);
    sendTo(accessGatewayAddr_, TaskConfigResp{head, true, estimated});
}

void AiAgora::failConfig(const UserHead &head)
{
    pool_.getContext<AiAgoraContext>(head.sessionTaskId)
        .useOrFailed(
            [&](AiAgoraContext &ctx)
            {
                if (ctx.state == kStateConfiguring)
                {
                    ctx.state = kStateUnconfigured;
                }
                sendTo(accessGatewayAddr_, TaskConfigResp{head, false, 0});
            },
            [&]() { LG_WRN("AiAgora: no session context for 0x%x", head.sessionTaskId); });
}

} // namespace DPlane::session
