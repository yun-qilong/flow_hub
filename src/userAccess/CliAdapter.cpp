#include "userAccess/CliAdapter.hpp"
#include "common/Constants.hpp"
#include "fw/EoEnv.hpp"
#include "utils/SysLog.hpp"

#include <iostream>
#include <poll.h>
#include <sstream>

namespace userAccess
{

bool CliAdapter::readLine(std::string &line)
{
    pollfd pfd{};
    pfd.fd = 0;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 100) <= 0)
    {
        return false;
    }

    if (not std::getline(std::cin, line))
    {
        return false;
    }

    return not line.empty();
}

bool CliAdapter::readFrontend()
{
    std::string line;
    if (not readLine(line))
    {
        return false;
    }

    if (waiting_)
    {
        return false;
    }

    dispatchInput(line);
    return true;
}

void CliAdapter::dispatchInput(const std::string &line)
{
    if (state_ == State::EnteringKey)
    {
        sendApiKey(line);
    }
    else if (line[0] == '/')
    {
        handleCommand(line);
    }
    else if (state_ == State::HasGtid)
    {
        sendChatMessage(line);
    }
    else
    {
        std::cout << "No active task. Use /new to create one.\n";
        showPrompt();
    }
}

void CliAdapter::showPrompt()
{
    if (waiting_)
    {
        std::cout << "... " << std::flush;
        return;
    }
    if (state_ == State::EnteringKey)
    {
        std::cout << "Enter API key: " << std::flush;
        return;
    }
    if (state_ == State::HasGtid)
    {
        std::cout << "[0x" << std::hex << currentGtid_ << std::dec << "]> " << std::flush;
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
    New,
    Quit,
    Help,
    Exit,
    Unknown
};

Cmd parseCmd(const std::string &cmd)
{
    if (cmd == "/new")
    {
        return Cmd::New;
    }
    if (cmd == "/quit")
    {
        return Cmd::Quit;
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
void CliAdapter::handleCommandImpl<CliAdapter::State::HaveNotGtid>(const std::string &line)
{
    std::istringstream iss(line);
    std::string token;
    iss >> token;

    switch (parseCmd(token))
    {
    case Cmd::New:
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

template <>
void CliAdapter::handleCommandImpl<CliAdapter::State::HasGtid>(const std::string &line)
{
    std::istringstream iss(line);
    std::string token;
    iss >> token;

    switch (parseCmd(token))
    {
    case Cmd::Quit:
        sendTaskDelete();
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
    case State::HaveNotGtid:
        handleCommandImpl<State::HaveNotGtid>(line);
        break;
    case State::EnteringKey:
        break;
    case State::HasGtid:
        handleCommandImpl<State::HasGtid>(line);
        break;
    }
}

void CliAdapter::sendTaskCreate()
{
    common::message::TaskCreateReq req;
    req.head.sessionTaskId = common::kInvalidGtid;
    req.taskType = common::TaskType::AiAgora;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr(), std::move(req));
}

void CliAdapter::sendTaskDelete()
{
    common::message::TaskDeleteReq req;
    req.head.sessionTaskId = currentGtid_;
    fw::anonSendTo(gatewayAddr(), std::move(req));
    resetState();
    std::cout << "Task deleted.\n";
    showPrompt();
}

void CliAdapter::sendChatMessage(const std::string &content)
{
    common::message::AiChatBusinessReq req;
    req.head.sessionTaskId = currentGtid_;
    req.head.busTaskIds = {currentGtid_};
    req.content = content;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr(), std::move(req));
    showPrompt();
}

void CliAdapter::sendApiKey(const std::string &key)
{
    if (key.empty())
    {
        std::cout << "API key cannot be empty.\n";
        showPrompt();
        return;
    }

    if (not aiApiAdapterAddr_)
    {
        std::cout << "AI adapter not configured.\n";
        state_ = State::HaveNotGtid;
        currentGtid_ = 0xFFFF;
        showPrompt();
        return;
    }

    fw::anonSendTo(aiApiAdapterAddr_, common::message::ApiKeyUpdate{key});
    state_ = State::HasGtid;
    std::cout << "API key set.\n";
    showPrompt();
}

void CliAdapter::showHelp()
{
    bool hasGtid = state_ == State::HasGtid;

    std::cout << "\nCommands:";
    std::cout << "\n  /new               Create a new task"
              << (not hasGtid ? "          ← available" : "");
    std::cout << "\n  /quit              Delete current task"
              << (hasGtid ? "               ← available" : "");
    std::cout << "\n  /exit              Exit program";
    std::cout << "\n  /help              Show this help";
    std::cout << "\n\n"
              << (hasGtid ? "Type any text to chat, or use /command.\n"
                          : "Use /new to create a task.\n");

    showPrompt();
}

void CliAdapter::resetState()
{
    state_ = State::HaveNotGtid;
    currentGtid_ = 0xFFFF;
    waiting_ = false;
}

void CliAdapter::handle(const common::message::TempConfig & /*cfg*/)
{
    gatewayAddr() = receiver().senderAddress();
}

void CliAdapter::handle(const common::message::AiChatBusinessResp &resp)
{
    waiting_ = false;
    std::cout << "\n" << resp.content << "\n";
    showPrompt();
}

void CliAdapter::handle(const common::message::TaskCreateResp &resp)
{
    waiting_ = false;
    if (resp.isSuccess and resp.head.sessionTaskId != common::kInvalidGtid)
    {
        currentGtid_ = resp.head.sessionTaskId;
        state_ = State::EnteringKey;
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
    LG_FEAT(AICHAT, "task deleted: sessionTaskId=0x%x", resp.head.sessionTaskId);
}

} // namespace userAccess
