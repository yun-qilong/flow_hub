#pragma once

#include "generated/message/Messages.hpp"
#include "userAccess/AccessAdapterBase.hpp"

#include <string>

namespace userAccess
{

struct NoConnection
{
};

class CliAdapter : public AccessAdapterBase<CliAdapter, NoConnection>
{
    using Base = AccessAdapterBase;

  public:
    explicit CliAdapter(caf::actor_system &sys) : Base(sys)
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::AiChatBusinessResp>();
        onMsg<common::message::TaskCreateResp>();
        onMsg<common::message::TaskDeleteResp>();
    }

    void handle(const common::message::TempConfig &cfg);
    void handle(const common::message::AiChatBusinessResp &resp);
    void handle(const common::message::TaskCreateResp &resp);
    void handle(const common::message::TaskDeleteResp &resp);

  public:
    void showPrompt();

    bool readFrontend();
    static bool readLine(std::string &line);
    void dispatchInput(const std::string &line);

    void setAiApiAdapterAddr(fw::EoAddress addr)
    {
        aiApiAdapterAddr_ = std::move(addr);
    }

  private:
    enum class State : uint8_t
    {
        HaveNotGtid,
        EnteringKey,
        HasGtid
    };

    State state_ = State::HaveNotGtid;
    bool waiting_ = false;

    template <State S>
    void handleCommandImpl(const std::string &line);
    void handleCommand(const std::string &line);
    void sendTaskCreate();
    void sendTaskDelete();
    void sendChatMessage(const std::string &content);
    void sendApiKey(const std::string &key);
    void showHelp();
    void resetState();

    uint16_t currentGtid_ = 0xFFFF;
    fw::EoAddress aiApiAdapterAddr_;
};

} // namespace userAccess
