#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace DPlane::session
{

class SessionDispatcher : public fw::EoBase<SessionDispatcher>
{
  public:
    explicit SessionDispatcher(fw::EoConfig &cfg, fw::EoAddress accessGatewayAddr);

    void handle(common::message::AiChatReq req);
    void handle(const common::message::TempConfig &msg);

    template <typename Msg>
    void handle(Msg msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::AiChatReq>();
        onMsg<common::message::AiChatResp>();
        onMsg<common::message::AiChatConfigResp>();
        onMsg<common::message::TaskConfigReq>();
        onMsg<common::message::AiAgoraChatReq>();
        onMsg<common::message::AiAgoraResetReq>();
        onMsg<common::message::TaskDeleteReq>();
    }

  private:
    static constexpr size_t kOrchestratorTableSize = 1024;

    template <typename Msg>
    void routeToOrchestrator(Msg &&msg);

    fw::EoAddress routerAddr_;
    std::array<fw::EoAddress, kOrchestratorTableSize> orchestratorTable_{};
};

template <typename Msg>
void SessionDispatcher::handle(Msg msg)
{
    routeToOrchestrator(std::move(msg));
}

template <typename Msg>
void SessionDispatcher::routeToOrchestrator(Msg &&msg)
{
    auto taskType = static_cast<size_t>(msg.head.sessionTaskId >> 6);
    if (taskType >= kOrchestratorTableSize)
    {
        LG_ERR("invalid taskType=0x%zx, dropping message", taskType);
        return;
    }
    auto addr = orchestratorTable_.at(taskType);
    if (not addr)
    {
        LG_ERR("no orchestrator for taskType=0x%zx, dropping message", taskType);
        return;
    }
    LG_DBG("routing sessionTaskId=0x%x to orchestrator",
           static_cast<unsigned>(msg.head.sessionTaskId));
    delegateTo(addr, std::forward<Msg>(msg));
}

} // namespace DPlane::session
