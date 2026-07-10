// src/DPlane/business/AiChatBus.hpp
// Data-plane business layer — AI Chat Scheduling EO
//
// 按 ADR-0010 规则：Business 层需要读写 Context 的 EO 通过 TaskType 模板参数
// 绑定业务类型。ContextType 由 ContextTypeOf<T> 编译期推导。
//
// 职责：
//   1. 收到 AiChatBusinessReq → 追加 user 消息到 Context → 转发 AiChatServiceReq 给 Adapter
//   2. 收到 AiChatServiceResp → 追加 assistant 消息到 Context → 回复 AiChatBusinessResp 给
//   SessionData

#pragma once

#include "fw/EoBase.hpp"
#include "generated/TaskType.hpp"
#include "generated/TaskTypeTraits.hpp"

namespace common
{
class TaskPool;
}

namespace common::message
{
struct AiChatServiceReq;
}

namespace DPlane::business
{

template <common::TaskType T>
class AiChatBus : public fw::EoBase<AiChatBus<T>>
{
  public:
    // 编译期推导 Context 类型（零开销）
    using ContextType = common::ContextTypeOf<T>;

    // pool: TaskPool 引用（Context 读写）
    // sessionDataAddr: SessionData actor（回复 AiChatBusinessResp / AiChatMsgAck 的目标）
    explicit AiChatBus(fw::EoConfig &cfg, common::TaskPool &pool, fw::EoAddress sessionDataAddr,
                       fw::EoAddress businessMgrAddr, fw::EoAddress routerAddr,
                       std::string defaultModelName = "default");

    void handle(const common::message::AiChatBusinessReq &req);
    void handle(const common::message::AiChatServiceResp &resp);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        this->template onMsg<common::message::TempConfig>();
        this->template onMsg<common::message::AiChatBusinessReq>();
        this->template onMsg<common::message::AiChatServiceResp>();
    }

  private:
    void processServiceRequest(ContextType &ctx, const common::message::AiChatBusinessReq &req,
                               uint16_t gtid);
    void processBusinessResp(ContextType &ctx, const common::message::AiChatServiceResp &resp);
    std::string buildMessagesJson(const ContextType &ctx, const std::string &content) const;
    void writeMessagesToContext(ContextType &ctx, const std::string &body);
    uint16_t allocateAndRecordSeq(ContextType &ctx);
    void sendAck(const common::message::UserHead &reqHead, uint16_t gtid, uint16_t seq,
                 const std::string &content);
    common::message::AiChatServiceReq
    buildAiChatServiceReq(const common::message::UserHead &reqHead, uint16_t gtid,
                          std::string messagesJson, const ContextType &ctx, uint16_t reqSeq);
    void appendAssistantMsg(ContextType &ctx, const std::string &content);

    common::TaskPool &pool_;
    fw::EoAddress sessionDataAddr_;
    fw::EoAddress businessMgrAddr_;
    fw::EoAddress routerAddr_;
    fw::EoAddress serviceGatewayAddr_;
    std::string defaultModelName_;
};

} // namespace DPlane::business
