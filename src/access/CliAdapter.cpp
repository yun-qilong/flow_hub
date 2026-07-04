#include "access/CliAdapter.hpp"

#include <iostream>

namespace access
{

bool CliAdapter::readFrontend()
{
    std::string line;
    if (not std::getline(std::cin, line))
    {
        return false;
    }

    if (line.empty())
    {
        return false;
    }

    std::cout << "> " << std::flush;
    return true;
}

void CliAdapter::handle(const common::message::TempConfig & /*cfg*/)
{
    gatewayAddr_ = receiver_.senderAddress();
}

void CliAdapter::handle(const common::message::AiChatBusinessResp &resp)
{
    std::cout << "\n" << resp.content << "\n> " << std::flush;
}

void CliAdapter::handle(const common::message::AiChatMsgAck & /*ack*/) {}

void CliAdapter::handle(const common::message::TaskSync &sync)
{
    if (sync.type == common::TaskSyncType::TaskDeleted)
    {
        std::cout << "[CliAdapter] task deleted: 0x" << std::hex << sync.gtid << std::dec << "\n> "
                  << std::flush;
    }
}

void CliAdapter::handle(const common::message::UserRegisterResp &resp)
{
    if (resp.success)
    {
        std::cout << "[CliAdapter] registered as " << resp.username << "\n> " << std::flush;
    }
    else
    {
        std::cout << "[CliAdapter] registration failed " << "\n> " << std::flush;
    }
}

void CliAdapter::handle(const common::message::UserLoginResp &resp)
{
    if (resp.success)
    {
        auto userId = common::getUserId(resp.head.uid);
        userToConn_.at(userId) = resp.connectionId;
        connToUser_.at(resp.connectionId) = userId;
        std::cout << "[CliAdapter] logged in\n> " << std::flush;
    }
}

void CliAdapter::handle(const common::message::UserLogoutResp &resp)
{
    if (not resp.success)
    {
        return;
    }
    auto userId = common::getUserId(resp.head.uid);
    auto connId = userToConn_.at(userId);
    if (connId != common::kInvalidConnectionId)
    {
        connToUser_.at(connId) = common::kInvalidUserId;
        userToConn_.at(userId) = common::kInvalidConnectionId;
    }
    std::cout << "[CliAdapter] logged out\n> " << std::flush;
}

void CliAdapter::handle(const common::message::TaskCreateResp &resp)
{
    if (resp.success)
    {
        std::cout << "[CliAdapter] task created: 0x" << std::hex << resp.head.gtidList.at(0)
                  << std::dec << "\n> " << std::flush;
    }
}

void CliAdapter::handle(const common::message::TaskDeleteResp& resp)
{
    if (resp.success)
    {
        std::cout << "[CliAdapter] task deleted\n> " << std::flush;
    }
}

} // namespace access
