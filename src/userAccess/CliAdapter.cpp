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
    if (in_ == &std::cin)
    {
        pollfd pfd{};
        pfd.fd = 0;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 100) <= 0)
        {
            return false;
        }
    }

    if (not std::getline(*in_, line))
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
    if (line.empty())
    {
        return;
    }

    if (line[0] == '/')
    {
        handleCommand(line);
        return;
    }

    if (state_ == State::EnteringKey)
    {
        sendApiKey(line);
        return;
    }

    if (state_ == State::HasGtid)
    {
        sendChatMessage(line);
        return;
    }

    *out_ << "No active task. Use /new to create one.\n";
    showPrompt();
}

void CliAdapter::showPrompt()
{
    if (waiting_)
    {
        *out_ << "... " << std::flush;
        return;
    }
    if (state_ == State::EnteringKey)
    {
        *out_ << "Enter API key: " << std::flush;
        return;
    }
    if (state_ == State::HasGtid)
    {
        *out_ << "[0x" << std::hex << currentGtid_ << std::dec << "]> " << std::flush;
    }
    else
    {
        *out_ << "> " << std::flush;
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
        *out_ << "Creating new task...\n";
        sendTaskCreate();
        return;
    case Cmd::Help:
        showHelp();
        return;
    case Cmd::Exit:
        exitCallback_();
        return;
    default:
        *out_ << "Unknown command. Use /help.\n";
        break;
    }
    showPrompt();
}

template <>
void CliAdapter::handleCommandImpl<CliAdapter::State::EnteringKey>(const std::string &line)
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
        exitCallback_();
        return;
    default:
        *out_ << "Unknown command. Use /help.\n";
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
        exitCallback_();
        return;
    default:
        *out_ << "Unknown command. Use /help.\n";
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
        handleCommandImpl<State::EnteringKey>(line);
        break;
    case State::HasGtid:
        handleCommandImpl<State::HasGtid>(line);
        break;
    }
}

void CliAdapter::sendTaskCreate()
{
    if (waiting_)
    {
        return;
    }

    common::message::TaskCreateReq req;
    req.head.sessionTaskId = common::kInvalidGtid;
    req.taskType = common::TaskType::AiAgora;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr(), std::move(req));
}

void CliAdapter::sendTaskDelete()
{
    if (waiting_)
    {
        *out_ << "Please wait for the current request.\n";
        showPrompt();
        return;
    }

    if (currentGtid_ == common::kInvalidGtid)
    {
        *out_ << "No active task.\n";
        showPrompt();
        return;
    }

    common::message::TaskDeleteReq req;
    req.head.sessionTaskId = currentGtid_;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr(), std::move(req));
}

void CliAdapter::sendChatMessage(const std::string &content)
{
    if (waiting_)
    {
        *out_ << "Please wait for the current request.\n";
        showPrompt();
        return;
    }

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
    if (waiting_)
    {
        *out_ << "Please wait for the current request.\n";
        showPrompt();
        return;
    }

    if (key.empty())
    {
        *out_ << "API key cannot be empty.\n";
        showPrompt();
        return;
    }

    if (not aiApiAdapterAddr_)
    {
        *out_ << "AI adapter not configured.\n";
        state_ = State::HaveNotGtid;
        currentGtid_ = 0xFFFF;
        showPrompt();
        return;
    }

    fw::anonSendTo(aiApiAdapterAddr_, common::message::ApiKeyUpdate{key});
    state_ = State::HasGtid;
    *out_ << "API key set.\n";
    showPrompt();
}

void CliAdapter::showHelp()
{
    bool hasGtid = state_ == State::HasGtid;

    *out_ << "\nCommands:";
    *out_ << "\n  /new               Create a new task"
          << (not hasGtid ? "          ← available" : "");
    *out_ << "\n  /quit              Delete current task"
          << (hasGtid ? "               ← available" : "");
    *out_ << "\n  /exit              Exit program";
    *out_ << "\n  /help              Show this help";
    *out_ << "\n\n"
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

void CliAdapter::pump()
{
    receiver().receive_for(kPollTimeout, messageHandler());
}

void CliAdapter::handle(const common::message::TempConfig & /*cfg*/)
{
    gatewayAddr() = receiver().senderAddress();
}

void CliAdapter::handle(const common::message::AiChatBusinessResp &resp)
{
    waiting_ = false;
    *out_ << "\n" << resp.content << "\n";
    showPrompt();
}

void CliAdapter::handle(const common::message::TaskCreateResp &resp)
{
    waiting_ = false;
    if (resp.isSuccess and resp.head.sessionTaskId != common::kInvalidGtid)
    {
        currentGtid_ = resp.head.sessionTaskId;
        state_ = State::EnteringKey;
        *out_ << "Task created: 0x" << std::hex << currentGtid_ << std::dec << "\n";
    }
    else
    {
        *out_ << "Task creation failed.\n";
    }
    showPrompt();
}

void CliAdapter::handle(const common::message::TaskDeleteResp &resp)
{
    LG_FEAT(AICHAT, "task deleted: sessionTaskId=0x%x", resp.head.sessionTaskId);

    if (not waiting_)
    {
        return;
    }

    if (resp.isSuccess)
    {
        *out_ << "Task deleted.\n";
        resetState();
    }
    else
    {
        *out_ << "Task deletion failed.\n";
        waiting_ = false;
    }
    showPrompt();
}

} // namespace userAccess
