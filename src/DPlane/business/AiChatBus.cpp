// src/DPlane/business/AiChatBus.cpp

#include "DPlane/business/AiChatBus.hpp"
#include "common/TaskPool.hpp"
#include "utils/JsonCoDec.hpp"

#include <cstring>

namespace DPlane::business
{

using TaskPool = common::TaskPool;
using AiChatBusinessReq = common::message::AiChatBusinessReq;
using GTID = common::GTID;
using UserHead = common::message::UserHead;
using AiChatServiceReq = common::message::AiChatServiceReq;
using AiChatBusinessResp = common::message::AiChatBusinessResp;
using AiChatServiceReq = common::message::AiChatServiceReq;
using AiChatServiceResp = common::message::AiChatServiceResp;

// 首轮自动预置的 system prompt（含尾部逗号分隔符）
static constexpr std::string_view SYSTEM_PROMPT =
    R"({"role":"system","content":"你是一个有帮助的AI助手。"},)";

template <common::TaskType T>
AiChatBus<T>::AiChatBus(fw::EoConfig &cfg, TaskPool &pool, fw::EoAddress sessionDataAddr,
                        fw::EoAddress businessMgrAddr, fw::EoAddress routerAddr,
                        std::string defaultModelName)
    : fw::EoBase<AiChatBus<T>>(cfg), pool_(pool), sessionDataAddr_(std::move(sessionDataAddr)),
      businessMgrAddr_(std::move(businessMgrAddr)), routerAddr_(std::move(routerAddr)),
      defaultModelName_(std::move(defaultModelName))
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
void AiChatBus<T>::handle(const AiChatBusinessReq &req)
{
    auto gtid = req.head.gtidList.at(0);
    LG_FEAT(AICHAT, "received AiChatBusinessReq: gtid=0x%x contentSize=%zuB", gtid,
            req.content.size());

    pool_.getContext<ContextType>(gtid).useOrFailed(
        [&](ContextType &ctx) { processServiceRequest(ctx, req, gtid); },
        [gtid]() { LG_ERR("no context for gtid=0x%x", gtid); });
}

template <common::TaskType T>
void AiChatBus<T>::handle(const AiChatServiceResp &resp)
{
    auto gtid = resp.head.gtidList.at(0);
    LG_FEAT(AICHAT, "received AiChatServiceResp: gtid=0x%x contentSize=%zuB", gtid,
            resp.content.size());

    pool_.getContext<ContextType>(gtid).useOrFailed(
        [&](ContextType &ctx) { processBusinessResp(ctx, resp); },
        [gtid]() { LG_ERR("no context for gtid=0x%x", gtid); });
}

template <common::TaskType T>
void AiChatBus<T>::processServiceRequest(ContextType &ctx, const AiChatBusinessReq &req, GTID gtid)
{
    uint16_t seq = allocateAndRecordSeq(ctx);
    std::string body = buildMessagesJson(ctx, req.content);
    writeMessagesToContext(ctx, body);

    LG_FEAT(AICHAT, "msg committed: seq=%u contentSize=%zuB%s", seq, req.content.size(),
            ctx.pendingReqSeq != 0 ? " (preempting)" : "");

    ctx.pendingReqSeq = seq;

    if (not serviceGatewayAddr_)
    {
        LG_ERR("serviceGatewayAddr not set");
        return;
    }

    auto serviceReq = buildAiChatServiceReq(req.head, gtid, "[" + std::move(body) + "]", ctx, seq);
    LG_FEAT(AICHAT, "sending AiChatServiceReq (reqSeq=%u) to gateway", seq);
    this->sendTo(serviceGatewayAddr_, std::move(serviceReq));
}

template <common::TaskType T>
void AiChatBus<T>::processBusinessResp(ContextType &ctx, const AiChatServiceResp &resp)
{
    if (ctx.pendingReqSeq == 0)
    {
        LG_ERR("no pending request, dropping AiChatServiceResp");
        return;
    }

    if (resp.reqSeq != ctx.pendingReqSeq)
    {
        LG_WRN("stale response discarded (resp.reqSeq=%u != pendingReqSeq=%u)", resp.reqSeq,
               ctx.pendingReqSeq);
        return;
    }

    if (resp.success)
    {
        allocateAndRecordSeq(ctx);
        appendAssistantMsg(ctx, resp.content);
    }

    if (not sessionDataAddr_)
    {
        LG_ERR("sessionDataAddr not set");
        return;
    }

    ctx.pendingReqSeq = 0;

    this->sendTo(sessionDataAddr_, AiChatBusinessResp{resp.head, resp.success, resp.content});
}

template <common::TaskType T>
uint16_t AiChatBus<T>::allocateAndRecordSeq(ContextType &ctx)
{
    int oldLen = ctx.messagesLen;
    ctx.messageCount++;
    uint16_t seq = ctx.messageCount;

    if (seq == 1)
    {
        ctx.messageOffsets.at(0) = static_cast<uint16_t>(SYSTEM_PROMPT.size());
    }
    else
    {
        ctx.messageOffsets.at(seq - 1) = static_cast<uint16_t>(oldLen + 1);
    }

    return seq;
}

template <common::TaskType T>
std::string AiChatBus<T>::buildMessagesJson(const ContextType &ctx,
                                            const std::string &content) const
{
    std::string userMsg = utils::JsonCoDec::buildMsgObj("user", content);

    if (ctx.messagesLen == 0)
    {
        return R"({"role":"system","content":"你是一个有帮助的AI助手。"},)" + userMsg;
    }

    auto *buf = reinterpret_cast<const char *>(ctx.messagesBuffer.data());
    return std::string(buf, ctx.messagesLen) + "," + userMsg;
}

template <common::TaskType T>
void AiChatBus<T>::writeMessagesToContext(ContextType &ctx, const std::string &body)
{
    int newLen = static_cast<int>(body.size());
    if (newLen <= static_cast<int>(ctx.messagesBuffer.size()))
    {
        std::memcpy(ctx.messagesBuffer.data(), body.data(), body.size());
        ctx.messagesLen = newLen;
    }
}

template <common::TaskType T>
AiChatServiceReq AiChatBus<T>::buildAiChatServiceReq(const UserHead &reqHead, uint16_t gtid,
                                                     std::string messagesJson,
                                                     const ContextType &ctx, uint16_t reqSeq)
{
    std::string modelName(
        reinterpret_cast<const char *>(ctx.modelName.data()),
        strnlen(reinterpret_cast<const char *>(ctx.modelName.data()), ctx.modelName.size()));

    AiChatServiceReq req;
    req.head.uid = reqHead.uid;
    req.head.accessType = reqHead.accessType;
    req.head.gtidList = {gtid};
    req.messagesJson = std::move(messagesJson);
    req.modelName = modelName.empty() ? defaultModelName_ : modelName;
    req.temperature = ctx.temperature > 0.0 ? ctx.temperature : 0.7;
    req.reqSeq = reqSeq;
    return req;
}

template <common::TaskType T>
void AiChatBus<T>::appendAssistantMsg(ContextType &ctx, const std::string &content)
{
    std::string append = "," + utils::JsonCoDec::buildMsgObj("assistant", content);
    int newLen = ctx.messagesLen + static_cast<int>(append.size());

    if (newLen > static_cast<int>(ctx.messagesBuffer.size()))
    {
        LG_ERR("messagesBuffer overflow! need %d max %zu", newLen, ctx.messagesBuffer.size());
        return;
    }

    std::memcpy(&ctx.messagesBuffer.at(ctx.messagesLen), append.data(), append.size());
    ctx.messagesLen = newLen;
}

template class AiChatBus<common::TaskType::AiAgora>;

} // namespace DPlane::business
