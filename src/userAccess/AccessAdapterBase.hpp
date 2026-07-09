#pragma once

#include "common/Constants.hpp"
#include "common/SessionFlags.hpp"
#include "common/UidUtil.hpp"
#include "fw/EoTypes.hpp"
#include "fw/ScopedEo.hpp"
#include "generated/type/AccessType.hpp"
#include "generated/type/AppType.hpp"
#include "generated/Types.hpp"
#include "utils/CrtpBase.hpp"

#include <array>
#include <chrono>
#include <cstdint>

namespace userAccess
{

template <typename Derived, common::AppType kApp, common::AccessType kAcc, typename Connection,
          uint16_t kPollTimeoutMs = 10>
class AccessAdapterBase : public utils::CrtpBase<Derived>
{
  public:
    static constexpr common::AppType kAppType = kApp;
    static constexpr common::AccessType kAccessType = kAcc;
    static constexpr common::SessionFlags kSessionFlags = common::SessionFlags::make<kAppType>();
    static constexpr auto kPollTimeout = std::chrono::milliseconds(kPollTimeoutMs);

    explicit AccessAdapterBase(caf::actor_system &sys) : receiver_(sys)
    {
        userToConn_.fill(common::kInvalidConnectionId);
        connToUser_.fill(common::kInvalidUserId);
    }

    fw::EoAddress myAddress()
    {
        return receiver_.address();
    }

    void tempSetGatewayAddr(fw::EoAddress addr)
    {
        gatewayAddr_ = std::move(addr);
    }

    void run()
    {
        while (true)
        {
            this->getImplementation().readFrontend();
            receiver_.receive_for(kPollTimeout, messageHandler_);
        }
    }

  protected:
    fw::ScopedEo receiver_;

    std::array<common::UserId, common::kMaxClientsPerAccess> connToUser_{};
    std::array<common::ConnectionId, common::kMaxUsers> userToConn_{};
    std::array<Connection, common::kMaxClientsPerAccess> connections_{};

    template <typename Msg>
    void fillHead(Msg &msg)
    {
        msg.head.accessType = kAccessType;
        msg.head.appType = kAppType;
        msg.head.sessionFlags = kSessionFlags;
        msg.head.uid = common::kInvalidUid;
        msg.head.targets = 0;
    }

    template <typename Msg>
    void setUidInHead(Msg &msg, common::ConnectionId connId)
    {
        auto uid = common::makeUid(connToUser_.at(connId), kAppType);
        msg.head.uid = uid;
    }

    template <typename Msg, typename F>
    void on(F &&handler)
    {
        static_assert(not std::is_const_v<std::remove_reference_t<Msg>>,
                      "message handler must take non-const reference for receive_for");
        auto mh = fw::MessageHandler{[this, h = std::forward<F>(handler)](Msg &m) { h(m); }};
        if (messageHandler_)
        {
            messageHandler_ = messageHandler_.or_else(std::move(mh));
        }
        else
        {
            messageHandler_ = std::move(mh);
        }
    }

    template <typename Msg>
    void onMsg()
    {
        on<Msg>([this](Msg &msg) { this->getImplementation().handle(std::move(msg)); });
    }

    fw::EoAddress gatewayAddr_;

  private:
    fw::MessageHandler messageHandler_;
};

} // namespace userAccess
