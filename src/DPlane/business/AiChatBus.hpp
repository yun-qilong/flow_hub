#pragma once

#include "fw/EoBase.hpp"
#include "generated/TaskType.hpp"
#include "generated/TaskTypeTraits.hpp"

#include <array>
#include <string>

namespace common
{
class TaskPool;
}

namespace common::message
{
struct AiChatConfigReq;
struct AiChatReq;
struct AiChatServiceReq;
struct AiChatServiceResp;
struct UserHead;
} // namespace common::message

namespace DPlane::business
{

template <common::TaskType T>
class AiChatBus : public fw::EoBase<AiChatBus<T>>
{
  public:
    using ContextType = common::ContextTypeOf<T>;

    explicit AiChatBus(fw::EoConfig &cfg, common::TaskPool &pool,
                       fw::EoAddress sessionDispatcherAddr, fw::EoAddress businessMgrAddr,
                       fw::EoAddress routerAddr);

    void handle(const common::message::AiChatConfigReq &req);
    void handle(const common::message::AiChatReq &req);
    void handle(const common::message::AiChatServiceResp &resp);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        this->template onMsg<common::message::TempConfig>();
        this->template onMsg<common::message::AiChatConfigReq>();
        this->template onMsg<common::message::AiChatReq>();
        this->template onMsg<common::message::AiChatServiceResp>();
    }

  private:
    bool isValidAiIndex(uint8_t aiIndex) const;
    bool applyConfig(ContextType &ctx, const common::message::AiChatConfigReq &req);
    void sendConfigResp(const common::message::UserHead &head, uint8_t aiIndex, bool isSuccess);
    void sendChatResp(const common::message::UserHead &head, bool success,
                      const std::string &content, uint8_t aiIndex);
    void sendChatToService(const ContextType &ctx, const std::string &messagesJson,
                           const common::message::UserHead &head);
    common::GTID firstBusTaskId(const common::message::UserHead &head) const;
    common::message::AiChatServiceReq buildServiceReq(const common::message::UserHead &head,
                                                      const std::string &messagesJson,
                                                      const ContextType &ctx) const;
    template <size_t N>
    std::string readCString(const std::array<uint8_t, N> &data) const;
    template <size_t N>
    bool writeCString(std::array<uint8_t, N> &data, const std::string &value);

    common::TaskPool &pool_;
    fw::EoAddress sessionDispatcherAddr_;
    fw::EoAddress businessMgrAddr_;
    fw::EoAddress routerAddr_;
    fw::EoAddress serviceGatewayAddr_;
};

} // namespace DPlane::business
