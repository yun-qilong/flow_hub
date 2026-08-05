#include "userAccess/AccessGateway.hpp"

namespace userAccess
{

using TaskCreateReq = common::message::TaskCreateReq;
using TaskCreateResp = common::message::TaskCreateResp;
using TaskDeleteReq = common::message::TaskDeleteReq;
using TaskDeleteResp = common::message::TaskDeleteResp;
using AiChatBusinessReq = common::message::AiChatBusinessReq;
using AiChatBusinessResp = common::message::AiChatBusinessResp;
using TempConfig = common::message::TempConfig;

AccessGateway::AccessGateway(fw::EoConfig &cfg, const fw::EoAddress &cliAdapter)
    : fw::EoBase<AccessGateway>(cfg), cliAdapterAddr_(cliAdapter)
{
    sendTo(cliAdapter, TempConfig{0});
}

void AccessGateway::handle(const TaskCreateReq &req)
{
    auto copy = TaskCreateReq{req};
    copy.cookie.adapterAddr = cliAdapterAddr_;
    delegateTo(sessionMgrAddr_, std::move(copy));
}

void AccessGateway::handle(TaskCreateResp resp)
{
    if (resp.isSuccess)
    {
        auto idx = static_cast<size_t>(resp.head.sessionTaskId & 0x0FFF);
        gtidToAdapter_.at(idx) = resp.cookie.adapterAddr;
    }
    delegateTo(cliAdapterAddr_, std::move(resp));
}

void AccessGateway::handle(const TaskDeleteReq &req)
{
    delegateTo(sessionMgrAddr_, TaskDeleteReq{req});
}

void AccessGateway::handle(TaskDeleteResp resp)
{
    auto idx = static_cast<size_t>(resp.head.sessionTaskId & 0x0FFF);
    gtidToAdapter_.at(idx) = fw::EoAddress{};
    delegateTo(cliAdapterAddr_, std::move(resp));
}

void AccessGateway::handle(AiChatBusinessReq req)
{
    delegateTo(sessionDispatcherAddr_, std::move(req));
}

void AccessGateway::handle(AiChatBusinessResp resp)
{
    auto idx = static_cast<size_t>(resp.head.sessionTaskId & 0x0FFF);
    auto adapter = gtidToAdapter_.at(idx);
    if (adapter)
    {
        delegateTo(adapter, std::move(resp));
    }
    else
    {
        LG_ERR("no adapter mapped for sessionTaskId=0x%x, dropping AiChatBusinessResp",
               resp.head.sessionTaskId);
    }
}

void AccessGateway::handle(const TempConfig &msg)
{
    if (msg.tag == 1)
    {
        sessionMgrAddr_ = senderAddress();
    }
    else if (msg.tag == 2)
    {
        sessionDispatcherAddr_ = senderAddress();
    }
}

} // namespace userAccess
