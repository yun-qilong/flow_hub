#include "userAccess/AccessGateway.hpp"

namespace userAccess
{

AccessGateway::AccessGateway(fw::EoConfig &cfg, const fw::EoAddress &cliAdapter)
    : fw::EoBase<AccessGateway>(cfg)
{
    adapterTable_.at(static_cast<size_t>(common::AccessType::AiAgoraCLI)) = cliAdapter;
    sendTo(cliAdapter, common::message::TempConfig{0});
}

template <typename Msg>
void AccessGateway::routeToAdapters(Msg &msg)
{
    auto targets = msg.head.targets;
    if (targets == 0)
    {
        forwardToAdapter(msg);
    }
    else
    {
        fanOutToAdapters(msg, targets);
    }
}

template <typename Msg>
void AccessGateway::fanOutToAdapters(const Msg &msg, uint64_t targets)
{
    while (targets != 0)
    {
        auto i = static_cast<size_t>(__builtin_ctzll(targets));
        targets &= targets - 1;
        if (adapterTable_.at(i))
        {
            sendTo(adapterTable_.at(i), Msg{msg});
        }
    }
}

template <typename Msg>
void AccessGateway::forwardToAdapter(Msg &msg)
{
    auto accType = static_cast<size_t>(msg.head.accessType);
    if (adapterTable_.at(accType))
    {
        delegateTo(adapterTable_.at(accType), std::move(msg));
    }
}

void AccessGateway::handle(const common::message::TaskCreateReq &req)
{
    delegateTo(sessionMgrAddr_, common::message::TaskCreateReq{req});
}

void AccessGateway::handle(common::message::TaskCreateResp resp)
{
    routeToAdapters(resp);
}

void AccessGateway::handle(const common::message::TaskDeleteReq &req)
{
    delegateTo(sessionMgrAddr_, common::message::TaskDeleteReq{req});
}

void AccessGateway::handle(common::message::TaskDeleteResp resp)
{
    routeToAdapters(resp);
}

void AccessGateway::handle(common::message::AiChatBusinessReq req)
{
    delegateTo(sessionDataAddr_, std::move(req));
}

void AccessGateway::handle(const common::message::TempConfig &msg)
{
    if (msg.tag == 1)
    {
        sessionMgrAddr_ = senderAddress();
    }
    else if (msg.tag == 2)
    {
        sessionDataAddr_ = senderAddress();
    }
}

void AccessGateway::handle(common::message::AiChatBusinessResp resp)
{
    routeToAdapters(resp);
}

} // namespace userAccess
