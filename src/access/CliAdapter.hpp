#pragma once

#include "access/AccessAdapterBase.hpp"
#include "generated/message/Messages.hpp"
#include "generated/type/AccessType.hpp"
#include "generated/type/AppType.hpp"

namespace access
{

struct NoConnection
{
};

class CliAdapter : public AccessAdapterBase<CliAdapter, common::AppType::AiChat,
                                            common::AccessType::AiChatCLI, NoConnection>
{
    using Base = AccessAdapterBase;

  public:
    explicit CliAdapter(caf::actor_system &sys) : Base(sys)
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::AiChatBusinessResp>();
        onMsg<common::message::AiChatMsgAck>();
        onMsg<common::message::TaskSync>();
        onMsg<common::message::UserRegisterResp>();
        onMsg<common::message::UserLoginResp>();
        onMsg<common::message::UserLogoutResp>();
        onMsg<common::message::TaskCreateResp>();
        onMsg<common::message::TaskDeleteResp>();
    }

    void handle(const common::message::TempConfig &cfg);
    void handle(const common::message::AiChatBusinessResp &resp);
    void handle(const common::message::AiChatMsgAck &ack);
    void handle(const common::message::TaskSync &sync);
    void handle(const common::message::UserRegisterResp &resp);
    void handle(const common::message::UserLoginResp &resp);
    void handle(const common::message::UserLogoutResp &resp);
    void handle(const common::message::TaskCreateResp &resp);
    void handle(const common::message::TaskDeleteResp &resp);

  protected:
    bool readFrontend();
};

} // namespace access
