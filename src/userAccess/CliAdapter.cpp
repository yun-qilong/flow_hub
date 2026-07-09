#include "userAccess/CliAdapter.hpp"
#include "common/Constants.hpp"
#include "fw/EoEnv.hpp"

#include <iostream>
#include <poll.h>
#include <sstream>

namespace userAccess
{

bool CliAdapter::readFrontend()
{
    pollfd pfd{};
    pfd.fd = 0;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 100) <= 0)
    {
        return false;
    }

    std::string line;
    if (not std::getline(std::cin, line))
    {
        return false;
    }

    if (line.empty())
    {
        return false;
    }

    if (waiting_)
    {
        return false;
    }

    if (line[0] == '/')
    {
        handleCommand(line);
    }
    else if (state_ == State::LoggedInWithGtid)
    {
        sendChatMessage(line);
    }
    else
    {
        std::cout << "Not logged in. Use /register or /login first.\n";
        showPrompt();
    }

    return true;
}

void CliAdapter::showPrompt()
{
    if (waiting_)
    {
        std::cout << "... " << std::flush;
        return;
    }
    if (state_ == State::LoggedInWithGtid)
    {
        std::cout << currentUsername_ << "[0x" << std::hex << currentGtid_ << std::dec << "]> "
                  << std::flush;
    }
    else
    {
        std::cout << "> " << std::flush;
    }
}

namespace
{

enum class Cmd : uint8_t
{
    Register,
    Login,
    Delete,
    Logout,
    New,
    Help,
    Exit,
    Unknown
};

Cmd parseCmd(const std::string &cmd)
{
    if (cmd == "/register")
    {
        return Cmd::Register;
    }
    if (cmd == "/login")
    {
        return Cmd::Login;
    }
    if (cmd == "/delete")
    {
        return Cmd::Delete;
    }
    if (cmd == "/logout")
    {
        return Cmd::Logout;
    }
    if (cmd == "/new")
    {
        return Cmd::New;
    }
    if (cmd == "/help")
    {
        return Cmd::Help;
    }
    if (cmd == "/exit")
    {
        return Cmd::Exit;
    }
    return Cmd::Unknown;
}

} // namespace

template <>
void CliAdapter::handleCommandImpl<CliAdapter::State::NotLoggedIn>(const std::string &line)
{
    std::istringstream iss(line);
    std::string token;
    iss >> token;
    auto cmd = parseCmd(token);

    switch (cmd)
    {
    case Cmd::Register:
    {
        std::string username;
        iss >> username;
        if (username.empty())
        {
            std::cout << "Usage: /register <username>\n";
            break;
        }
        sendRegister(username);
        return;
    }
    case Cmd::Login:
    {
        std::string username;
        iss >> username;
        if (username.empty())
        {
            std::cout << "Usage: /login <username>\n";
            break;
        }
        sendLogin(username);
        return;
    }
    case Cmd::Delete:
    {
        std::string username;
        iss >> username;
        if (username.empty())
        {
            std::cout << "Usage: /delete <username>\n";
            break;
        }
        sendDelete();
        return;
    }
    case Cmd::Help:
        showHelp();
        return;
    case Cmd::Exit:
        std::exit(0);
    default:
        std::cout << "[Wrn] Unknown command. Use /help.\n";
        break;
    }
    showPrompt();
}

template <>
void CliAdapter::handleCommandImpl<CliAdapter::State::LoggedInWithGtid>(const std::string &line)
{
    std::istringstream iss(line);
    std::string token;
    iss >> token;

    switch (parseCmd(token))
    {
    case Cmd::Logout:
        sendLogout();
        return;
    case Cmd::New:
        currentGtid_ = 0xFFFF;
        std::cout << "Creating new task...\n";
        sendTaskCreate();
        return;
    case Cmd::Help:
        showHelp();
        return;
    case Cmd::Exit:
        std::exit(0);
    default:
        std::cout << "Unknown command. Use /help.\n";
        break;
    }
    showPrompt();
}

void CliAdapter::handleCommand(const std::string &line)
{
    switch (state_)
    {
    case State::NotLoggedIn:
        handleCommandImpl<State::NotLoggedIn>(line);
        break;
    case State::LoggedInWithGtid:
        handleCommandImpl<State::LoggedInWithGtid>(line);
        break;
    }
}

void CliAdapter::sendRegister(const std::string &username)
{
    common::message::UserRegisterReq req;
    fillHead(req);
    req.username = username;
    req.connectionId = kConnectionId;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr_, std::move(req));
}

void CliAdapter::sendLogin(const std::string &username)
{
    common::message::UserLoginReq req;
    fillHead(req);
    req.username = username;
    req.connectionId = kConnectionId;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr_, std::move(req));
}

void CliAdapter::sendLogout()
{
    if (state_ == State::NotLoggedIn)
    {
        std::cout << "Not logged in.\n";
        showPrompt();
        return;
    }
    common::message::UserLogoutReq req;
    fillHead(req);
    req.head.uid = currentUid_;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr_, std::move(req));
}

void CliAdapter::sendDelete()
{
    common::message::UserDeleteReq req;
    fillHead(req);
    req.head.uid = currentUid_;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr_, std::move(req));
}

void CliAdapter::sendTaskCreate()
{
    common::message::TaskCreateReq req;
    fillHead(req);
    req.head.uid = currentUid_;
    req.taskType = common::TaskType::AiChat;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr_, std::move(req));
}

void CliAdapter::sendChatMessage(const std::string &content)
{
    common::message::AiChatBusinessReq req;
    fillHead(req);
    req.head.uid = currentUid_;
    req.head.gtidList = {currentGtid_};
    req.content = content;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr_, std::move(req));
    showPrompt();
}

void CliAdapter::showHelp()
{
    bool loggedIn = state_ == State::LoggedInWithGtid;

    std::cout << "\nCommands:";

    std::cout << "\n  /register <name>   Register a new user"
              << (not loggedIn ? "          ← available" : "");
    std::cout << "\n  /login <name>      Login"
              << (not loggedIn ? "                        ← available" : "");
    std::cout << "\n  /delete <name>     Delete account"
              << (not loggedIn ? "               ← available" : "");
    std::cout << "\n  /logout            Logout"
              << (loggedIn ? "                        ← available" : "");
    std::cout << "\n  /new               Create new task"
              << (loggedIn ? "              ← available" : "");
    std::cout << "\n  /exit              Exit program";
    std::cout << "\n  /help              Show this help";
    std::cout << "\n\n"
              << (loggedIn ? "Type any text to chat, or use /command.\n"
                           : "Use /register and /login to get started.\n");

    showPrompt();
}

void CliAdapter::resetState()
{
    state_ = State::NotLoggedIn;
    currentUsername_.clear();
    currentUid_ = 0xFFFF;
    currentGtid_ = 0xFFFF;
}

void CliAdapter::handle(const common::message::TempConfig & /*cfg*/)
{
    gatewayAddr_ = receiver_.senderAddress();
}

void CliAdapter::handle(const common::message::AiChatBusinessResp &resp)
{
    waiting_ = false;
    std::cout << "\n" << resp.content << "\n";
    showPrompt();
}

void CliAdapter::handle(const common::message::AiChatMsgAck & /*ack*/) {}

void CliAdapter::handle(const common::message::TaskSync &sync)
{
    if (sync.type == common::TaskSyncType::TaskDeleted)
    {
        std::cout << "\n[Task deleted: 0x" << std::hex << sync.gtid << std::dec << "]\n";
        showPrompt();
    }
}

void CliAdapter::handle(const common::message::UserRegisterResp &resp)
{
    waiting_ = false;
    if (resp.success)
    {
        std::cout << "Registered as '" << resp.username << "'.\n";
    }
    else
    {
        std::cout << "Registration failed.\n";
    }
    showPrompt();
}

void CliAdapter::handle(const common::message::UserLoginResp &resp)
{
    if (resp.success)
    {
        currentUsername_ = resp.username;
        currentUid_ = resp.head.uid;
        auto userId = common::getUserId(resp.head.uid);
        userToConn_.at(userId) = kConnectionId;
        connToUser_.at(kConnectionId) = userId;
        std::cout << "Logged in as '" << currentUsername_ << "'.\n";
        sendTaskCreate();
    }
    else
    {
        waiting_ = false;
        std::cout << "Login failed.\n";
        showPrompt();
    }
}

void CliAdapter::handle(const common::message::UserLogoutResp &resp)
{
    waiting_ = false;
    if (resp.success)
    {
        auto userId = common::getUserId(currentUid_);
        userToConn_.at(userId) = common::kInvalidConnectionId;
        connToUser_.at(kConnectionId) = common::kInvalidUserId;
        std::cout << "Logged out.\n";
        resetState();
    }
    showPrompt();
}

void CliAdapter::handle(const common::message::TaskCreateResp &resp)
{
    waiting_ = false;
    if (resp.success and not resp.head.gtidList.empty())
    {
        currentGtid_ = resp.head.gtidList.at(0);
        state_ = State::LoggedInWithGtid;
        std::cout << "Task created: 0x" << std::hex << currentGtid_ << std::dec << "\n";
    }
    else
    {
        std::cout << "Task creation failed.\n";
    }
    showPrompt();
}

void CliAdapter::handle(const common::message::TaskDeleteResp &resp)
{
    if (resp.success)
    {
        currentGtid_ = 0xFFFF;
        std::cout << "Task deleted. Creating new task...\n";
        sendTaskCreate();
    }
    else
    {
        waiting_ = false;
        showPrompt();
    }
}

} // namespace userAccess
