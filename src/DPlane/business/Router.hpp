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
        return routeTable_.at(gtid >> 6);
    }

    template <typename Msg>
    void routeAndForward(Msg msg, const char *msgName)
    {
        const auto &list = msg.head.busTaskIds;
        if (list.empty())
        {
            LG_ERR("empty busTaskIds, dropping %s", msgName);
            return;
        }

        routeCopies(msg, list.size() - 1, msgName);
        routeLast(std::move(msg), msgName);
    }

    template <typename Msg>
    void routeCopies(const Msg &msg, size_t copyCount, const char *msgName)
    {
        const auto &list = msg.head.busTaskIds;
        for (size_t i = 0; i < copyCount; ++i)
        {
            auto addr = getTargetEoAddress(list.at(i));
            if (addr)
            {
                auto copy = Msg{msg};
                copy.head.busTaskIds = {list.at(i)};
                LG_DBG("routing %s to Business EO (copy)", msgName);
                sendTo(addr, std::move(copy));
            }
        }
    }

    template <typename Msg>
    void routeLast(Msg msg, const char *msgName)
    {
        const auto &list = msg.head.busTaskIds;
        auto lastAddr = getTargetEoAddress(list.at(list.size() - 1));
        if (lastAddr)
        {
            msg.head.busTaskIds = {list.at(list.size() - 1)};
            LG_DBG("routing %s to Business EO (delegate)", msgName);
            delegateTo(lastAddr, std::move(msg));
        }
        else
        {
            LG_ERR("last busTaskId no route, dropping %s", msgName);
        }
    }
};

} // namespace DPlane::business
