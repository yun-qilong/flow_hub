#include "DPlane/session/AiAgora.hpp"
#include "common/TaskPool.hpp"
#include "utils/TryCatch.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace DPlane::session
{

using AiAgoraContext = common::context::AiAgoraContext;
using AiAgoraChatReq = common::message::AiAgoraChatReq;
using AiAgoraChatResp = common::message::AiAgoraChatResp;
using AiAgoraResetReq = common::message::AiAgoraResetReq;
using AiAgoraResetResp = common::message::AiAgoraResetResp;
using TaskDeleteReq = common::message::TaskDeleteReq;
using TaskDeleteResp = common::message::TaskDeleteResp;
using AiChatReq = common::message::AiChatReq;
using AiChatResp = common::message::AiChatResp;
using TaskConfigReq = common::message::TaskConfigReq;
using TaskConfigResp = common::message::TaskConfigResp;
using BusTaskCreateReq = common::message::BusTaskCreateReq;
using BusTaskCreateResp = common::message::BusTaskCreateResp;
using BusTaskDeleteReq = common::message::BusTaskDeleteReq;
using BusTaskDeleteResp = common::message::BusTaskDeleteResp;
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
                    [this, cfg = parsed](BusTaskCreateResp &resp) { onBusTaskCreated(cfg, resp); },
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

std::vector<common::GTID> AiAgora::collectBusTaskIds(AiAgoraContext &ctx)
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
    return gtids;
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
        ctx.state = kStateUnconfigured;
        auto reqHead = resp.head;
        reqHead.busTaskIds = collectBusTaskIds(ctx);
        requestThen(
            sessionMgrAddr_, std::chrono::milliseconds(ctx.timeoutMs), BusTaskDeleteReq{reqHead},
            [this, head = resp.head](BusTaskDeleteResp &) { failConfig(head); },
            [this, head = resp.head](const caf::error &) { failConfig(head); });
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
    resetContext(ctx);
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

void AiAgora::handle(const AiAgoraChatReq &req)
{
    pool_.getContext<AiAgoraContext>(req.head.sessionTaskId)
        .useOrFailed([&](AiAgoraContext &ctx) { processChatRequest(ctx, req); }, [&]()
                     { LG_WRN("AiAgora: no session context for 0x%x", req.head.sessionTaskId); });
}

void AiAgora::processChatRequest(AiAgoraContext &ctx, const AiAgoraChatReq &req)
{
    if (ctx.state != kStateWaitingForTopic)
    {
        LG_WRN("AiAgora: chat rejected, state=%u for 0x%x", static_cast<unsigned>(ctx.state),
               req.head.sessionTaskId);
        sendTo(accessGatewayAddr_,
               buildChatResp(req.head, true, false, 0, kErrorInvalidState, ctx.state, ""));
        return;
    }
    auto remaining = ctx.topicBaseJson.size() - ctx.topicBaseJsonSize;
    if (remaining < ctx.maxCharPerTopic)
    {
        LG_WRN("AiAgora: topic base full for 0x%x", req.head.sessionTaskId);
        sendTo(accessGatewayAddr_,
               buildChatResp(req.head, true, false, 0, kErrorContextFullAtStart, ctx.state, ""));
        return;
    }
    ctx.state = kStateWaitingForDebateReplies;
    sendAiChatRequest(ctx, req);
}

std::vector<common::GTID> AiAgora::collectActiveDebateIds(AiAgoraContext &ctx)
{
    std::vector<common::GTID> targets;
    for (uint8_t i = 0; i < 8; ++i)
    {
        auto gtid = ctx.debateTaskIds.at(i);
        if (gtid != common::kInvalidGtid)
        {
            targets.push_back(gtid);
            ctx.pendingReplies |= static_cast<uint16_t>(1U << i);
        }
    }
    return targets;
}

void AiAgora::sendAiChatRequest(AiAgoraContext &ctx, const AiAgoraChatReq &req)
{
    auto targets = collectActiveDebateIds(ctx);
    if (targets.empty())
    {
        ctx.pendingReplies = 0;
        ctx.state = kStateWaitingForTopic;
        sendTo(accessGatewayAddr_,
               buildChatResp(req.head, true, false, 0, kErrorNoBusTask, ctx.state, ""));
        return;
    }
    auto item = buildTopicMessage("user", req.content);
    if (not appendTopicMessage(ctx, item))
    {
        ctx.pendingReplies = 0;
        ctx.state = kStateWaitingForTopic;
        sendTo(accessGatewayAddr_,
               buildChatResp(req.head, true, false, 0, kErrorContextFullAtStart, ctx.state, ""));
        return;
    }
    auto messagesJson = std::string(reinterpret_cast<const char *>(ctx.topicBaseJson.data()),
                                    ctx.topicBaseJsonSize);
    AiChatReq aiReq;
    aiReq.head = req.head;
    aiReq.head.busTaskIds = std::move(targets);
    aiReq.messagesJson = std::move(messagesJson);
    sendTo(routerAddr_, std::move(aiReq));
}

void AiAgora::handle(const AiChatResp &resp)
{
    pool_.getContext<AiAgoraContext>(resp.head.sessionTaskId)
        .useOrFailed([&](AiAgoraContext &ctx) { onAiChatResp(ctx, resp); }, [&]()
                     { LG_WRN("AiAgora: no session context for 0x%x", resp.head.sessionTaskId); });
}

void AiAgora::onAiChatResp(AiAgoraContext &ctx, const AiChatResp &resp)
{
    if (ctx.state != kStateWaitingForDebateReplies)
    {
        return;
    }
    if (resp.aiIndex >= 8 or (ctx.pendingReplies & (1U << resp.aiIndex)) == 0)
    {
        LG_WRN("AiAgora: unexpected aiIndex=%u for 0x%x", static_cast<unsigned>(resp.aiIndex),
               resp.head.sessionTaskId);
        return;
    }
    if (not resp.success)
    {
        failChat(ctx, resp.head, kErrorNetworkTimeout);
        return;
    }
    ctx.pendingReplies &= static_cast<uint16_t>(~(1U << resp.aiIndex));
    if (ctx.pendingReplies == 0)
    {
        completeChat(ctx, resp.head, resp.content);
    }
}

void AiAgora::completeChat(AiAgoraContext &ctx, const UserHead &head, const std::string &content)
{
    auto item = buildTopicMessage("assistant", content);
    if (not appendTopicMessage(ctx, item))
    {
        failChat(ctx, head, kErrorContextFullMidRound);
        return;
    }
    ctx.state = kStateWaitingForTopic;
    sendTo(accessGatewayAddr_,
           buildChatResp(head, true, true, kEndReasonNoJudge, kErrorNoError, ctx.state, content));
}

void AiAgora::failChat(AiAgoraContext &ctx, const UserHead &head, uint8_t errorCode)
{
    ctx.state = kStateWaitingForTopic;
    ctx.pendingReplies = 0;
    sendTo(accessGatewayAddr_, buildChatResp(head, true, false, 0, errorCode, ctx.state, ""));
}

void AiAgora::handle(const AiAgoraResetReq &req)
{
    pool_.getContext<AiAgoraContext>(req.head.sessionTaskId)
        .useOrFailed(
            [&](AiAgoraContext &ctx)
            {
                if (ctx.state == kStateWaitingForTopic or
                    ctx.state == kStateWaitingForDebateReplies or
                    ctx.state == kStateWaitingForJudgeVerdict)
                {
                    resetContext(ctx);
                    auto estimated =
                        static_cast<uint16_t>(common::kTopicBaseJsonSize / ctx.maxCharPerTopic);
                    sendTo(accessGatewayAddr_, AiAgoraResetResp{req.head, true, estimated});
                    return;
                }
                sendTo(accessGatewayAddr_, AiAgoraResetResp{req.head, false, 0});
            },
            [&]()
            {
                LG_WRN("AiAgora: no session context for 0x%x", req.head.sessionTaskId);
                sendTo(accessGatewayAddr_, AiAgoraResetResp{req.head, false, 0});
            });
}

void AiAgora::handle(const TaskDeleteReq &req)
{
    pool_.getContext<AiAgoraContext>(req.head.sessionTaskId)
        .useOrFailed(
            [&](AiAgoraContext &ctx)
            {
                auto reqHead = req.head;
                reqHead.busTaskIds = collectBusTaskIds(ctx);
                requestThen(
                    sessionMgrAddr_, std::chrono::milliseconds(ctx.timeoutMs),
                    BusTaskDeleteReq{reqHead},
                    [this, req](BusTaskDeleteResp &) { finishDelete(req); },
                    [this, req](const caf::error &) { finishDelete(req); });
            },
            [&]()
            {
                LG_WRN("AiAgora: no session context for 0x%x", req.head.sessionTaskId);
                finishDelete(req);
            });
}

void AiAgora::finishDelete(const TaskDeleteReq &req)
{
    sendTo(sessionMgrAddr_, TaskDeleteReq{req});
    sendTo(accessGatewayAddr_, TaskDeleteResp{req.head, true});
}

void AiAgora::resetContext(AiAgoraContext &ctx)
{
    ctx.topicBaseJsonSize = 2;
    ctx.topicBaseJson.at(0) = static_cast<uint8_t>('[');
    ctx.topicBaseJson.at(1) = static_cast<uint8_t>(']');
    ctx.lastRoundResponses.fill(0);
    ctx.currentRound = 0;
    ctx.pendingReplies = 0;
    ctx.state = kStateWaitingForTopic;
}

bool AiAgora::appendTopicMessage(AiAgoraContext &ctx, const std::string &item)
{
    auto commaLen = (ctx.topicBaseJsonSize == 2) ? 0U : 1U;
    auto extra = commaLen + static_cast<uint32_t>(item.size());
    if (ctx.topicBaseJsonSize + extra > ctx.topicBaseJson.size())
    {
        return false;
    }
    auto insertAt = ctx.topicBaseJsonSize - 1;
    ctx.topicBaseJson.at(insertAt + extra) = static_cast<uint8_t>(']');
    if (commaLen != 0)
    {
        ctx.topicBaseJson.at(insertAt) = static_cast<uint8_t>(',');
        insertAt += 1;
    }
    std::memcpy(ctx.topicBaseJson.data() + insertAt, item.data(), item.size());
    ctx.topicBaseJsonSize += extra;
    return true;
}

std::string AiAgora::buildTopicMessage(const std::string &role, const std::string &content)
{
    nlohmann::json item = {{"role", role}, {"content", content}};
    return item.dump();
}

AiAgoraChatResp AiAgora::buildChatResp(const UserHead &head, bool isComplete, bool hasResponses,
                                       uint8_t endReason, uint8_t errorCode, uint8_t currentState,
                                       const std::string &responses)
{
    AiAgoraChatResp resp;
    resp.head = head;
    resp.isComplete = isComplete;
    resp.hasResponses = hasResponses;
    resp.endReason = endReason;
    resp.errorCode = errorCode;
    resp.currentState = currentState;
    resp.responses = responses;
    return resp;
}

} // namespace DPlane::session
