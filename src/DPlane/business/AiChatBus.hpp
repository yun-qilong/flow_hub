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
    using ContextType = common::ContextTypeOf<T>;

    explicit AiChatBus(fw::EoConfig &cfg, common::TaskPool &pool,
                       fw::EoAddress sessionDispatcherAddr, fw::EoAddress businessMgrAddr,
                       fw::EoAddress routerAddr, std::string defaultModelName = "default");

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
    common::message::AiChatServiceReq
    buildAiChatServiceReq(const common::message::UserHead &reqHead, uint16_t gtid,
                          std::string messagesJson, const ContextType &ctx, uint16_t reqSeq);
    void appendAssistantMsg(ContextType &ctx, const std::string &content);

    common::TaskPool &pool_;
    fw::EoAddress sessionDispatcherAddr_;
    fw::EoAddress businessMgrAddr_;
    fw::EoAddress routerAddr_;
    fw::EoAddress serviceGatewayAddr_;
    std::string defaultModelName_;
};

} // namespace DPlane::business
