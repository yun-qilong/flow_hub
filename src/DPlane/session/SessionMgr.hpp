// src/DPlane/session/SessionMgr.hpp
// Data-plane session layer — Session Manager (GTID lifecycle)
//
// 负责 GTID 的分配与回收，作为会话生命周期的管控入口。

#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"
#include "generated/Types.hpp"
#include "utils/StaticVector.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace common
{
class TaskPool;
}

namespace DPlane::session
{

struct UserRecord
{
    char name[32]{};
    utils::StaticVector<common::GTID, common::kMaxGtidsPerUser> gtids;

    void clear()
    {
        name[0] = '\0';
        gtids.clear();
    }
};

class SessionMgr : public fw::EoBase<SessionMgr>
{
  public:
    explicit SessionMgr(fw::EoConfig &cfg, common::TaskPool &pool, fw::EoAddress accessGatewayAddr,
                        fw::EoAddress sessionDataAddr);

    void handle(const common::message::UserRegisterReq &req);
    void handle(const common::message::UserLoginReq &req);
    void handle(const common::message::UserLogoutReq &req);
    void handle(const common::message::UserDeleteReq &req);
    void handle(const common::message::TaskCreateReq &req);
    void handle(const common::message::TaskDeleteReq &req);
    void handle(const common::message::TempConfig &msg);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::UserRegisterReq>();
        onMsg<common::message::UserLoginReq>();
        onMsg<common::message::UserLogoutReq>();
        onMsg<common::message::UserDeleteReq>();
        onMsg<common::message::TaskCreateReq>();
        onMsg<common::message::TaskDeleteReq>();
    }

  private:
    static constexpr bool isTaskTypeAllowed(common::AppType app, common::TaskType task)
    {
        switch (app)
        {
        case common::AppType::AiChat:
            return task == common::TaskType::AiChat;
        case common::AppType::AiDiscussion:
            return task == common::TaskType::AiChat;
        }
        return false;
    }

    common::UserId allocateUserId();
    common::message::UserLoginSessionReq
    buildLoginSessionReq(const common::message::UserHead &head, uint16_t uid,
                         const utils::StaticVector<common::GTID, common::kMaxGtidsPerUser> &gtids);
    void sendRegisterResp(common::message::UserRegisterResp &resp, uint16_t uid, bool success);
    void sendLoginResp(common::message::UserLoginResp &resp, uint16_t uid, bool success,
                       bool needWaitForData,
                       utils::StaticVector<common::GTID, common::kMaxGtidsPerUser> gtids);
    std::string findUsernameByUserId(common::UserId userId) const;
    void deallocateUserGtids(common::UserId userId, common::AppType appType);
    bool eraseGtid(common::UserId userId, common::AppType appType, common::GTID gtid);
    void releaseUserId(common::UserId userId);

    common::TaskPool &pool_;
    fw::EoAddress accessGatewayAddr_;
    fw::EoAddress sessionDataAddr_;
    fw::EoAddress businessMgrAddr_;

    std::unordered_map<std::string, common::UserId> usernameToId_;
    std::array<std::array<UserRecord, common::kMaxAppTypes>, common::kMaxUsers> userRecords_{};
    std::bitset<common::kMaxUsers> uidBitset_;
};

} // namespace DPlane::session
