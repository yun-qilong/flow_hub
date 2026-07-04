// src/DPlane/business/Router.hpp
// Data-plane business layer — Message Router EO
//
// 职责：根据 GTID[11:6] 提取 TaskType，将入向消息路由到对应 Business EO。
// 仅发往 Business D 面 EO 的消息经过 Router（ADR-0011）。

#pragma once

#include "fw/EoBase.hpp"

#include <array>

namespace DPlane::business
{

class Router : public fw::EoBase<Router>
{
  public:
    explicit Router(fw::EoConfig &cfg, fw::EoAddress businessMgrAddr,
                    fw::EoAddress sessionDataAddr);

    void handle(const common::message::RouterConfigReq &req);
    void handle(const common::message::RouterReconfigReq &req);
    void handle(common::message::AiChatBusinessReq req);
    void handle(common::message::AiChatServiceResp resp);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::RouterConfigReq>();
        onMsg<common::message::RouterReconfigReq>();
        onMsg<common::message::AiChatBusinessReq>();
        onMsg<common::message::AiChatServiceResp>();
    }

  private:
    static constexpr uint16_t kRouteTableSize = 1024;
    std::array<fw::EoAddress, kRouteTableSize> routeTable_{};
    fw::EoAddress businessMgrAddr_;

    fw::EoAddress getTargetEoAddress(uint16_t gtid) const
    {
        return routeTable_[gtid >> 6];
    }

    template <typename Msg>
    void routeAndForward(Msg msg, const char *msgName)
    {
        const auto &list = msg.head.gtidList;
        size_t n = list.size();
        if (n == 0)
        {
            std::cerr << "[Router] ERROR: empty gtidList, dropping " << msgName << "\n";
            return;
        }

        for (size_t i = 0; i < n - 1; ++i)
        {
            auto addr = getTargetEoAddress(list.at(i));
            if (addr)
            {
                std::cout << "[Router] routing " << msgName << " to Business EO (copy)\n";
                sendTo(addr, Msg{msg});
            }
        }

        auto lastAddr = getTargetEoAddress(list.at(n - 1));
        if (lastAddr)
        {
            std::cout << "[Router] routing " << msgName << " to Business EO (delegate)\n";
            delegateTo(lastAddr, std::move(msg));
        }
        else
        {
            std::cerr << "[Router] ERROR: last GTID no route, dropping " << msgName << "\n";
        }
    }
};

} // namespace DPlane::business
