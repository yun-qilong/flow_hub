#include "userAccess/CliAdapter.hpp"
#include "common/Constants.hpp"
#include "fw/EoEnv.hpp"
#include "utils/SysLog.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
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

    dispatchText(line);
}

void CliAdapter::dispatchText(const std::string &line)
{
    if (state_ == State::EnteringKey)
    {
        sendApiKey(line);
        return;
    }

    if (state_ == State::Configuring)
    {
        *out_ << "Please wait for configuration.\n";
        showPrompt();
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

constexpr const char *kDefaultApiUrl = "https://api.deepseek.com";
constexpr const char *kDefaultModel = "deepseek-v4-flash";
constexpr const char *kDefaultSystemPrompt = "You are a helpful assistant.";

enum class Cmd : uint8_t
{
    New,
    Quit,
    Reset,
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
    if (cmd == "/reset")
    {
        return Cmd::Reset;
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
    case Cmd::Reset:
        sendReset();
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
    case Cmd::Reset:
        sendReset();
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
    case Cmd::Reset:
        sendReset();
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
    case State::Configuring:
        *out_ << "Please wait for configuration.\n";
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

void CliAdapter::sendReset()
{
    if (currentGtid_ == common::kInvalidGtid)
    {
        *out_ << "No active task.\n";
        showPrompt();
        return;
    }

    // reset aborts the in-flight topic, so it bypasses the waiting_ guard.
    common::message::AiAgoraResetReq req;
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

    common::message::AiAgoraChatReq req;
    req.head.sessionTaskId = currentGtid_;
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
    sendTaskConfig(key);
}

void CliAdapter::sendTaskConfig(const std::string &apiKey)
{
    common::message::TaskConfigReq req;
    req.head.sessionTaskId = currentGtid_;
    req.payload = buildSingleAiPayload(apiKey);
    state_ = State::Configuring;
    waiting_ = true;
    fw::anonSendTo(gatewayAddr(), std::move(req));
    *out_ << "Configuring session...\n";
}

std::string CliAdapter::buildSingleAiPayload(const std::string &apiKey)
{
    const char *apiUrl = std::getenv("FLOWHUB_API_URL");
    const char *model = std::getenv("FLOWHUB_MODEL");

    nlohmann::json payload;
    payload["aiCount"] = 1;
    payload["hasJudge"] = false;
    payload["maxRounds"] = 5;
    payload["maxResponseLength"] = 500;
    payload["timeoutMs"] = 30000;

    nlohmann::json config;
    config["apiUrl"] = apiUrl != nullptr ? apiUrl : kDefaultApiUrl;
    config["apiKey"] = apiKey;
    config["model"] = model != nullptr ? model : kDefaultModel;
    config["systemPrompt"] = kDefaultSystemPrompt;
    config["temperature"] = 0.7;
    payload["configs"] = nlohmann::json::array({config});

    return payload.dump();
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

void CliAdapter::handle(const common::message::AiAgoraChatResp &resp)
{
    waiting_ = false;
    *out_ << "\n" << resp.responses << "\n";
    showPrompt();
}

void CliAdapter::handle(const common::message::AiAgoraResetResp &resp)
{
    waiting_ = false;
    if (resp.isSuccess)
    {
        *out_ << "Session reset. estimatedTopicCount=" << resp.estimatedTopicCount << "\n";
    }
    else
    {
        *out_ << "Reset failed.\n";
    }
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

void CliAdapter::handle(const common::message::TaskConfigResp &resp)
{
    waiting_ = false;
    if (resp.isSuccess)
    {
        state_ = State::HasGtid;
        *out_ << "Session configured: 0x" << std::hex << currentGtid_ << std::dec << "\n";
    }
    else
    {
        state_ = State::HaveNotGtid;
        currentGtid_ = 0xFFFF;
        *out_ << "Session configuration failed.\n";
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
