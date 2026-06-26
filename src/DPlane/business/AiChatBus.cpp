// src/DPlane/business/AiChatBus.cpp

#include "DPlane/business/AiChatBus.hpp"
#include "common/TaskPool.hpp"
#include "utils/JsonCoDec.hpp"

#include <cstring>
#include <iostream>

namespace DPlane::business
{

using namespace common;
using namespace common::message;

// 首轮自动预置的 system prompt（含尾部逗号分隔符）
static constexpr std::string_view SYSTEM_PROMPT =
    R"({"role":"system","content":"你是一个有帮助的AI助手。"},)";

template <common::TaskType T>
AiChatBus<T>::AiChatBus(fw::EoConfig &cfg, TaskPool &pool, fw::EoAddress gatewayAddr,
                        std::string defaultModelName)
    : fw::EoBase<AiChatBus<T>>(cfg), pool_(pool), gatewayAddr_(std::move(gatewayAddr)),
      defaultModelName_(std::move(defaultModelName))
{
}

template <common::TaskType T>
void AiChatBus<T>::handle(const AiChatBusinessReq &req)
{
    auto gtid = req.head.gtidList[0];
    std::cout << "[AiChatBus] received AiChatBusinessReq: gtid=0x" << std::hex << gtid << std::dec
              << " content=" << req.content << "\n";

    pool_.getContext<ContextType>(gtid).useOrFailed(
        [&](ContextType &ctx) { processServiceRequest(ctx, req, gtid); },
        [gtid]()
        {
            std::cerr << "[AiChatBus] ERROR: no context for gtid=0x" << std::hex << gtid << std::dec
                      << "\n";
        });
}

template <common::TaskType T>
void AiChatBus<T>::handle(const AiChatServiceResp &resp)
{
    auto gtid = resp.head.gtidList[0];
    std::cout << "[AiChatBus] received AiChatServiceResp: gtid=0x" << std::hex << gtid << std::dec
              << " content=" << resp.content << "\n";

    pool_.getContext<ContextType>(gtid).useOrFailed(
        [&](ContextType &ctx) { processBusinessResp(ctx, resp, gtid); },
        [gtid]()
        {
            std::cerr << "[AiChatBus] ERROR: no context for gtid=0x" << std::hex << gtid << std::dec
                      << "\n";
        });
}

template <common::TaskType T>
void AiChatBus<T>::processServiceRequest(ContextType &ctx, const AiChatBusinessReq &req, GTID gtid)
{
    ctx.businessReplyAddr = req.head.sourceAddress;

    uint16_t seq = allocateAndRecordSeq(ctx);
    std::string body = buildMessagesJson(ctx, req.content);
    writeMessagesToContext(ctx, body);

    sendAck(ctx, gtid, seq, req.content);

    std::cout << "[AiChatBus] msg committed: seq=" << seq << " content=" << req.content
              << (ctx.pendingReqSeq != 0 ? " (preempting)" : "") << "\n";

    ctx.pendingReqSeq = seq;

    if (not gatewayAddr_)
    {
        std::cerr << "[AiChatBus] ERROR: gatewayAddr not set\n";
        return;
    }

    auto serviceReq = buildAiChatServiceReq(gtid, "[" + std::move(body) + "]", ctx, seq);
    std::cout << "[AiChatBus] sending AiChatServiceReq (reqSeq=" << seq << ") to gateway\n";
    this->sendTo(gatewayAddr_, std::move(serviceReq));
}

template <common::TaskType T>
void AiChatBus<T>::processBusinessResp(ContextType &ctx, const AiChatServiceResp &resp, GTID gtid)
{
    if (ctx.pendingReqSeq == 0)
    {
        std::cerr << "[AiChatBus] ERROR: no pending request, dropping AiChatServiceResp\n";
        return;
    }

    if (resp.reqSeq != ctx.pendingReqSeq)
    {
        std::cout << "[AiChatBus] stale response discarded (resp.reqSeq=" << resp.reqSeq
                  << " != pendingReqSeq=" << ctx.pendingReqSeq << ")\n";
        return;
    }

    if (resp.success)
    {
        allocateAndRecordSeq(ctx);
        appendAssistantMsg(ctx, resp.content);
    }

    if (not ctx.businessReplyAddr)
    {
        std::cerr << "[AiChatBus] ERROR: businessReplyAddr not set\n";
        return;
    }

    ctx.pendingReqSeq = 0;

    auto target = ctx.businessReplyAddr;
    ctx.businessReplyAddr = {};

    this->sendTo(target, AiChatBusinessResp{resp.head, resp.success, resp.content});
}

template <common::TaskType T>
uint16_t AiChatBus<T>::allocateAndRecordSeq(ContextType &ctx)
{
    int oldLen = ctx.messagesLen;
    uint16_t seq = ctx.messageCount;
    ctx.messageCount++;

    if (seq == 0)
    {
        ctx.messageOffsets.at(seq) = static_cast<uint16_t>(SYSTEM_PROMPT.size());
    }
    else
    {
        ctx.messageOffsets.at(seq) = static_cast<uint16_t>(oldLen + 1);
    }

    return seq;
}

template <common::TaskType T>
void AiChatBus<T>::sendAck(const ContextType &ctx, uint16_t gtid, uint16_t seq,
                           const std::string &content)
{
    AiChatMsgAck ack;
    ack.head.gtidList = {gtid};
    ack.seq = seq;
    ack.content = content;
    this->sendTo(ctx.businessReplyAddr, std::move(ack));
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
        auto *buf = reinterpret_cast<char *>(ctx.messagesBuffer.data());
        std::memcpy(buf, body.data(), newLen);
        ctx.messagesLen = newLen;
    }
}

template <common::TaskType T>
AiChatServiceReq AiChatBus<T>::buildAiChatServiceReq(uint16_t gtid, std::string messagesJson,
                                                     const ContextType &ctx, uint16_t reqSeq)
{
    std::string modelName(
        reinterpret_cast<const char *>(ctx.modelName.data()),
        strnlen(reinterpret_cast<const char *>(ctx.modelName.data()), ctx.modelName.size()));

    AiChatServiceReq req;
    req.head.gtidList = {gtid};
    req.head.sourceAddress = this->senderAddress();
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
        std::cerr << "[AiChatBus] ERROR: messagesBuffer overflow! need " << newLen << " max "
                  << ctx.messagesBuffer.size() << "\n";
        return;
    }

    auto *buf = reinterpret_cast<char *>(ctx.messagesBuffer.data());
    std::memcpy(buf + ctx.messagesLen, append.data(), append.size());
    ctx.messagesLen = newLen;
}

template class AiChatBus<common::TaskType::AiChat>;

} // namespace DPlane::business
